/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "GPUSkinnedModelRenderer.h"

#include "graphics/MeshManager.h"
#include "graphics/Model.h"
#include "graphics/ModelDef.h"
#include "graphics/RenderableObject.h"
#include "graphics/ShaderManager.h"
#include "graphics/ShaderTechnique.h"
#include "graphics/ShaderTechniquePtr.h"
#include "lib/debug.h"
#include "lib/lib.h"
#include "maths/Matrix3D.h"
#include "maths/Vector3D.h"
#include "maths/Vector4D.h"
#include "ps/CLogger.h"
#include "ps/CStrIntern.h"
#include "ps/CStrInternStatic.h"
#include "ps/VideoMode.h"
#include "ps/containers/StaticVector.h"
#include "renderer/ModelRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/VertexArray.h"
#include "renderer/VertexBuffer.h"
#include "renderer/VertexBufferManager.h"
#include "renderer/backend/Barrier.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/IShaderProgram.h"
#include "third_party/mikktspace/weldmesh.h"

#include <algorithm>
#include <iterator>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace
{

// We have the following structure for input and output vertices:
//
// Created on a model load and read by a compute shader for each frame where
// the model is visible.
//  InputVertex(size/stride=48):
//   vec4/CVector4D tangent (offset=0)
//   vec3/CVector3D normal (offset=16)
//   vec3/CVector3D position (offset=32)
//
// Written by a compute shader for each frame where the model is visible.
//  OutputPosition(size/stride=16):
//   vec3/CVector3D position (offset=0)
//
// Written by a compute shader for each frame where the model is visible.
// Each component is 16-bits float to consume less memory.
//  OutputNormalTangent(size/stride=16)
//   16bits vec3 normal (offset=0)
//   16bits vec4 tangent (offset=8)

constexpr uint32_t INPUT_VERTEX_ATTRIBUTE_ALIGNMENT{16};
constexpr uint32_t INPUT_VERTEX_TANGENT_OFFSET{0};
constexpr uint32_t INPUT_VERTEX_NORMAL_OFFSET{16};
constexpr uint32_t INPUT_VERTEX_POSITION_OFFSET{32};

constexpr uint32_t OUTPUT_POSITION_STRIDE{16};
constexpr uint32_t OUTPUT_NORMAL_TANGENT_STRIDE{16};
constexpr uint32_t OUTPUT_NORMAL_OFFSET{0};
constexpr uint32_t OUTPUT_TANGENT_OFFSET{8};

class ModelDefRData : public CModelDefRPrivate
{
public:
	// Static per-CModel vertex array
	VertexArray m_Array;

	// Position and normals are static
	VertexArray::Attribute m_Position;
	VertexArray::Attribute m_Normal;
	VertexArray::Attribute m_Tangent;

	VertexArray m_BlendArray;
	VertexArray::Attribute m_BlendJoints;
	VertexArray::Attribute m_BlendWeights;

	VertexArray m_UVArray;

	// The number of UVs is determined by the model
	std::vector<VertexArray::Attribute> m_UVs;

	Renderer::Backend::IVertexInputLayout* m_VertexInputLayout{nullptr};

	// Indices are the same for all models, so share them
	VertexIndexArray m_IndexArray;

	ModelDefRData(const CModelDefPtr& mdef);
};

ModelDefRData::ModelDefRData(const CModelDefPtr& modelDef)
	: m_IndexArray(Renderer::Backend::IBuffer::Usage::TRANSFER_DST),
	m_Array(Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::TRANSFER_DST |
			Renderer::Backend::IBuffer::Usage::STORAGE),
	m_BlendArray(Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::TRANSFER_DST |
			Renderer::Backend::IBuffer::Usage::STORAGE),
	m_UVArray(Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::TRANSFER_DST)
{
	m_Position.format = Renderer::Backend::Format::R32G32B32_SFLOAT;
	m_Array.AddAttribute(&m_Position);

	m_Normal.format = Renderer::Backend::Format::R32G32B32_SFLOAT;
	m_Array.AddAttribute(&m_Normal);

	// TODO: switch to 16-bits tangents when possible.
	m_Tangent.format = Renderer::Backend::Format::R32G32B32A32_SFLOAT;
	m_Array.AddAttribute(&m_Tangent);

	m_UVs.resize(modelDef->GetNumUVsPerVertex());
	for (uint32_t index{0}; index < modelDef->GetNumUVsPerVertex(); ++index)
	{
		m_UVs[index].format = Renderer::Backend::Format::R32G32_SFLOAT;
		m_UVArray.AddAttribute(&m_UVs[index]);
	}

	// Joint indices are bytes and 0xFF means no joint.
	// Add 1 to NumBones because of the special 'root' bone.
	if (modelDef->GetNumBones() + 1 > 255)
		LOGERROR("Model '%s' has too many bones %zu/255", modelDef->GetName().string8().c_str(), modelDef->GetNumBones() + 1);
	ENSURE(modelDef->GetNumBones() + 1 <= 255);

	m_BlendJoints.format = Renderer::Backend::Format::R8G8B8A8_UINT;
	m_BlendArray.AddAttribute(&m_BlendJoints);

	m_BlendWeights.format = Renderer::Backend::Format::R8G8B8A8_UNORM;
	m_BlendArray.AddAttribute(&m_BlendWeights);

	// Generate tangents for the geometry:

	// floats per vertex; position + normal + tangent + UV*sets + GPUskinning (joint index and weight)
	const uint32_t numberOfFloatsPerVertex{
		static_cast<uint32_t>(3 + 3 + 4 + 2 * modelDef->GetNumUVsPerVertex() + 8)};

	// the tangent generation can increase the number of vertices temporarily
	// so reserve a bit more memory to avoid reallocations in GenTangents (in most cases)
	std::vector<float> newVertices;
	newVertices.reserve(numberOfFloatsPerVertex * modelDef->GetNumVertices() * 2);

	// Generate the tangents.
	ModelRenderer::GenTangents(modelDef, newVertices, true);

	// How many vertices do we have after generating tangents?
	const uint32_t newNumberOfVertices{static_cast<uint32_t>(newVertices.size()) / numberOfFloatsPerVertex};

	std::vector<int> remapTable(newNumberOfVertices);
	std::vector<float> vertexDataOut(newNumberOfVertices * numberOfFloatsPerVertex);

	// Re-weld the mesh to remove duplicated vertices.
	const int finalNumberOfVertices{WeldMesh(
		remapTable.data(), vertexDataOut.data(), newVertices.data(), newNumberOfVertices, numberOfFloatsPerVertex)};

	// Copy the model data to graphics memory.

	m_Array.SetNumberOfVertices(finalNumberOfVertices);
	m_Array.SetMinimumAttributeAlignment(INPUT_VERTEX_ATTRIBUTE_ALIGNMENT);
	m_Array.Layout();

	m_BlendArray.SetNumberOfVertices(finalNumberOfVertices);
	m_BlendArray.Layout();

	m_UVArray.SetNumberOfVertices(finalNumberOfVertices);
	m_UVArray.Layout();

	VertexArrayIterator<CVector3D> positionIt{m_Position.GetIterator<CVector3D>()};
	VertexArrayIterator<CVector3D> normalIt{m_Normal.GetIterator<CVector3D>()};
	VertexArrayIterator<CVector4D> tangentIt{m_Tangent.GetIterator<CVector4D>()};

	VertexArrayIterator<std::uint8_t[4]> blendJointsIt{m_BlendJoints.GetIterator<std::uint8_t[4]>()};
	VertexArrayIterator<std::uint8_t[4]> blendWeightsIt{m_BlendWeights.GetIterator<std::uint8_t[4]>()};

	// Copy everything into the vertex array.
	for (int index{0}; index < finalNumberOfVertices; ++index)
	{
		uint32_t inputDataOffset{numberOfFloatsPerVertex * index};

		positionIt[index] = CVector3D{
			vertexDataOut[inputDataOffset + 0],
			vertexDataOut[inputDataOffset + 1],
			vertexDataOut[inputDataOffset + 2]};
		inputDataOffset += 3;

		normalIt[index] = CVector3D{
			vertexDataOut[inputDataOffset + 0],
			vertexDataOut[inputDataOffset + 1],
			vertexDataOut[inputDataOffset + 2]};
		inputDataOffset += 3;

		tangentIt[index] = CVector4D{
			vertexDataOut[inputDataOffset + 0],
			vertexDataOut[inputDataOffset + 1],
			vertexDataOut[inputDataOffset + 2],
			vertexDataOut[inputDataOffset + 3]};
		inputDataOffset += 4;

		for (uint32_t j{0}; j < 4; ++j)
		{
			blendJointsIt[index][j] = static_cast<std::uint8_t>(
				vertexDataOut[inputDataOffset + 0 + 2 * j]);
			blendWeightsIt[index][j] = static_cast<std::uint8_t>(
				vertexDataOut[inputDataOffset + 1 + 2 * j]);
		}
		inputDataOffset += 8;

		for (uint32_t uvIndex{0}; uvIndex < modelDef->GetNumUVsPerVertex(); uvIndex++)
		{
			VertexArrayIterator<float[2]> UVit{m_UVs[uvIndex].GetIterator<float[2]>()};
			UVit[index][0] = vertexDataOut[inputDataOffset + 0 + 2 * uvIndex];
			UVit[index][1] = vertexDataOut[inputDataOffset + 1 + 2 * uvIndex];
		}
	}

	// Upload vertex data.
	m_Array.Upload();
	m_Array.FreeBackingStore();

	m_BlendArray.Upload();
	m_BlendArray.FreeBackingStore();

	if (m_UVArray.GetStride() > 0)
	{
		m_UVArray.Upload();
		m_UVArray.FreeBackingStore();
	}

	ENSURE(m_Array.GetStride() == INPUT_VERTEX_ATTRIBUTE_ALIGNMENT * 3);
	ENSURE(m_Position.offset == INPUT_VERTEX_POSITION_OFFSET);
	ENSURE(m_Normal.offset == INPUT_VERTEX_NORMAL_OFFSET);
	ENSURE(m_Tangent.offset == INPUT_VERTEX_TANGENT_OFFSET);

	ENSURE(m_BlendArray.GetStride() == 8);
	ENSURE(m_BlendJoints.offset % 4 == 0);
	ENSURE(m_BlendWeights.offset % 4 == 0);

	m_IndexArray.SetNumberOfVertices(modelDef->GetNumFaces() * 3);
	m_IndexArray.Layout();

	// Re-index geometry and upload index.
	VertexArrayIterator<std::uint16_t> indices{m_IndexArray.GetIterator()};
	for (uint32_t index{0}; index < modelDef->GetNumFaces() * 3; ++index)
		indices[index] = remapTable[index];
	m_IndexArray.Upload();
	m_IndexArray.FreeBackingStore();

	constexpr size_t MAX_UV{2};

	PS::StaticVector<Renderer::Backend::SVertexAttributeFormat, 4 + 3 + MAX_UV> attributes{
		{Renderer::Backend::VertexAttributeStream::POSITION,
			m_Position.format, 0, OUTPUT_POSITION_STRIDE,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0},
		{Renderer::Backend::VertexAttributeStream::NORMAL,
			Renderer::Backend::Format::R16G16B16A16_SFLOAT, OUTPUT_NORMAL_OFFSET, OUTPUT_NORMAL_TANGENT_STRIDE,
		Renderer::Backend::VertexAttributeRate::PER_VERTEX, 1},
		{Renderer::Backend::VertexAttributeStream::UV2,
		Renderer::Backend::Format::R16G16B16A16_SFLOAT, OUTPUT_TANGENT_OFFSET, OUTPUT_NORMAL_TANGENT_STRIDE,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 1}
	};

	for (size_t uv{0}; uv < std::min(MAX_UV, modelDef->GetNumUVsPerVertex()); ++uv)
	{
		const Renderer::Backend::VertexAttributeStream stream =
			static_cast<Renderer::Backend::VertexAttributeStream>(
				static_cast<int>(Renderer::Backend::VertexAttributeStream::UV0) + uv);
		attributes.push_back({
			stream, m_UVs[uv].format, m_UVs[uv].offset, m_UVArray.GetStride(),
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 2});
	}

	std::ranges::copy(ModelRenderer::GetInstanceTransformAttributes(), std::back_inserter(attributes));

	m_VertexInputLayout = g_Renderer.GetVertexInputLayout({attributes.begin(), attributes.end()});
}

struct ModelRData : public CModelRData
{
	// We have a separate position array because we don't need other attributes
	// for some passes (like shadows).
	CVertexBufferManager::Handle m_PositionHandle;
	CVertexBufferManager::Handle m_NormalTangentHandle;

	ModelRData(const void* key)
		: CModelRData(key)
	{}
};

/**
 * A skinned model to dispatch, with what its dispatch batches by.
 */
struct SkinningItem
{
	ModelDefRData* modelDefRData;
	CVertexBuffer* positions;
	CVertexBuffer* normalsTangents;
	const ModelRData* modelRData;
	CModel* model;

	auto Batch() const { return std::tie(modelDefRData, positions, normalsTangents); }
};

} // anonymous namespace

struct GPUSkinnedModelModelRenderer::Internals
{
	// Previously prepared modeldef
	ModelDefRData* modelDefRData;

	CShaderTechniquePtr skinningShaderTechnique;

	std::unique_ptr<Renderer::Backend::IBuffer> matricesBuffer;
	std::unique_ptr<Renderer::Backend::IBuffer> instancesBuffer;
	std::vector<SkinningItem> skinningItems;
};

GPUSkinnedModelModelRenderer::GPUSkinnedModelModelRenderer()
	: m(std::make_unique<Internals>())
{
	m->modelDefRData = nullptr;
	m->skinningShaderTechnique = g_Renderer.GetShaderManager().LoadEffect(str_compute_skinning);
}

GPUSkinnedModelModelRenderer::~GPUSkinnedModelModelRenderer() = default;

CModelRData* GPUSkinnedModelModelRenderer::CreateModelData(const void* key, CModel* model)
{
	ENSURE(model->IsSkinned());
	CModelDefPtr modelDef{model->GetModelDef()};
	ModelDefRData* modelDefRData{static_cast<ModelDefRData*>(modelDef->GetRenderData(m.get()))};

	if (!modelDefRData)
	{
		modelDefRData = new ModelDefRData(modelDef);
		modelDef->SetRenderData(m.get(), modelDefRData);
	}

	ModelRData* modelRData{new ModelRData(key)};

	const size_t numberOfVertices{modelDefRData->m_Array.GetNumberOfVertices()};
	modelRData->m_PositionHandle = g_Renderer.GetVertexBufferManager().AllocateChunk(
		OUTPUT_POSITION_STRIDE, numberOfVertices, Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::STORAGE,
		nullptr, CVertexBufferManager::Group::DEFAULT);
	modelRData->m_NormalTangentHandle = g_Renderer.GetVertexBufferManager().AllocateChunk(
		OUTPUT_NORMAL_TANGENT_STRIDE, numberOfVertices, Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::STORAGE,
		nullptr, CVertexBufferManager::Group::DEFAULT);

	return modelRData;
}

void GPUSkinnedModelModelRenderer::UpdateModelsData(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	std::span<CModel*> models)
{
	if (models.empty())
		return;

	GPU_SCOPED_LABEL(deviceCommandContext, "Compute Skinning");

	std::vector<SkinningItem>& items{m->skinningItems};
	items.clear();
	items.reserve(models.size());
	size_t numberOfMatrices{0};
	for (CModel* model : models)
	{
		ENSURE(model->IsSkinned());
		const CModelDefPtr& modelDef{model->GetModelDef()};
		const ModelRData* modelRData{static_cast<const ModelRData*>(model->GetRenderData())};
		ENSURE(modelRData->m_PositionHandle->m_Count == modelRData->m_NormalTangentHandle->m_Count);
		items.push_back({
			static_cast<ModelDefRData*>(modelDef->GetRenderData(m.get())),
			modelRData->m_PositionHandle->m_Owner, modelRData->m_NormalTangentHandle->m_Owner,
			modelRData, model});
		// Add 1 to NumBones because of the special 'root' bone.
		numberOfMatrices += modelDef->GetNumBones() + 1;
	}
	std::sort(items.begin(), items.end(), [](const SkinningItem& a, const SkinningItem& b)
	{
		return a.Batch() < b.Batch();
	});

	const auto upload = [&](std::unique_ptr<Renderer::Backend::IBuffer>& buffer, const char* name,
		const size_t size, const Renderer::Backend::IDeviceCommandContext::UploadBufferFunction& function)
	{
		if (!buffer || buffer->GetSize() < size)
		{
			buffer = g_VideoMode.GetBackendDevice()->CreateBuffer(
				name, Renderer::Backend::IBuffer::Type::VERTEX, static_cast<uint32_t>(size + size / 2),
				Renderer::Backend::IBuffer::Usage::DYNAMIC | Renderer::Backend::IBuffer::Usage::TRANSFER_DST |
					Renderer::Backend::IBuffer::Usage::STORAGE);
		}
		deviceCommandContext->UploadBufferRegion(buffer.get(), 0, static_cast<uint32_t>(size), function);
	};
	upload(m->matricesBuffer, "SkinningMatrices", numberOfMatrices * sizeof(CMatrix3D), [&items](std::uint8_t* data)
	{
		for (const SkinningItem& item : items)
		{
			const size_t size{(item.model->GetModelDef()->GetNumBones() + 1) * sizeof(CMatrix3D)};
			std::memcpy(data, item.model->GetAnimatedBoneMatrices(), size);
			data += size;
		}
	});
	upload(m->instancesBuffer, "SkinningInstances", items.size() * 2 * sizeof(uint32_t), [&items](std::uint8_t* data)
	{
		uint32_t* offsets{reinterpret_cast<uint32_t*>(data)};
		for (const SkinningItem& item : items)
		{
			*offsets++ = static_cast<uint32_t>(item.modelRData->m_PositionHandle->m_Index);
			*offsets++ = static_cast<uint32_t>(item.modelRData->m_NormalTangentHandle->m_Index);
		}
	});

	deviceCommandContext->InsertMemoryBarrier(
		Renderer::Backend::PipelineStage::VERTEX_INPUT, Renderer::Backend::PipelineStage::COMPUTE_SHADER,
		Renderer::Backend::Access::VERTEX_ATTRIBUTE_READ | Renderer::Backend::Access::INDEX_READ,
		Renderer::Backend::Access::SHADER_READ | Renderer::Backend::Access::SHADER_WRITE);
	deviceCommandContext->BeginComputePass();
	deviceCommandContext->SetComputePipelineState(
		m->skinningShaderTechnique->GetComputePipelineState());

	Renderer::Backend::IShaderProgram* shaderProgram{m->skinningShaderTechnique->GetShader()};
	const int32_t vertexCountSlot{shaderProgram->GetBindingSlot(str_vertexCount)};
	const int32_t offsetSlot{shaderProgram->GetBindingSlot(str_offset)};
	const int32_t batchSlot{shaderProgram->GetBindingSlot(str_batch)};
	const int32_t inputVerticesSlot{shaderProgram->GetBindingSlot(str_InputVertices)};
	const int32_t skinDataSlot{shaderProgram->GetBindingSlot(str_SkinData)};
	const int32_t outputPositionsSlot{shaderProgram->GetBindingSlot(str_OutputPositions)};
	const int32_t outputNormalsTangentsSlot{shaderProgram->GetBindingSlot(str_OutputNormalsTangents)};
	const int32_t matricesSlot{shaderProgram->GetBindingSlot(str_SkinningMatrices)};
	const int32_t instancesSlot{shaderProgram->GetBindingSlot(str_SkinningInstances)};

	constexpr uint32_t threadGroupWorkRegionDim{64};
	constexpr size_t maxDispatchInstances{65535};
	size_t firstMatrix{0};
	for (size_t begin{0}; begin < items.size();)
	{
		const SkinningItem& first{items[begin]};
		size_t end{begin + 1};
		while (end < items.size() && end - begin < maxDispatchInstances && items[end].Batch() == first.Batch())
			++end;

		const size_t numberOfInstances{end - begin};
		const size_t matricesPerInstance{first.model->GetModelDef()->GetNumBones() + 1};
		const uint32_t vertexCount{static_cast<uint32_t>(first.modelRData->m_PositionHandle->m_Count)};

		deviceCommandContext->SetUniform(vertexCountSlot, static_cast<float>(vertexCount));
		deviceCommandContext->SetUniform(offsetSlot,
			static_cast<float>(first.modelDefRData->m_Array.GetOffset()),
			static_cast<float>(first.modelDefRData->m_BlendArray.GetOffset()),
			static_cast<float>(begin),
			static_cast<float>(firstMatrix));
		deviceCommandContext->SetUniform(batchSlot,
			static_cast<float>(numberOfInstances), static_cast<float>(matricesPerInstance));
		deviceCommandContext->SetStorageBuffer(inputVerticesSlot, first.modelDefRData->m_Array.GetBuffer());
		deviceCommandContext->SetStorageBuffer(skinDataSlot, first.modelDefRData->m_BlendArray.GetBuffer());
		deviceCommandContext->SetStorageBuffer(outputPositionsSlot, first.positions->GetBuffer());
		deviceCommandContext->SetStorageBuffer(outputNormalsTangentsSlot, first.normalsTangents->GetBuffer());
		deviceCommandContext->SetStorageBuffer(matricesSlot, m->matricesBuffer.get());
		deviceCommandContext->SetStorageBuffer(instancesSlot, m->instancesBuffer.get());
		deviceCommandContext->Dispatch(
			DivideRoundUp(vertexCount, threadGroupWorkRegionDim), static_cast<uint32_t>(numberOfInstances), 1);

		firstMatrix += numberOfInstances * matricesPerInstance;
		begin = end;
	}

	deviceCommandContext->EndComputePass();
	deviceCommandContext->InsertMemoryBarrier(
		Renderer::Backend::PipelineStage::COMPUTE_SHADER, Renderer::Backend::PipelineStage::VERTEX_INPUT,
		Renderer::Backend::Access::SHADER_READ | Renderer::Backend::Access::SHADER_WRITE,
		Renderer::Backend::Access::VERTEX_ATTRIBUTE_READ | Renderer::Backend::Access::INDEX_READ);
}

void GPUSkinnedModelModelRenderer::UploadModelsData(Renderer::Backend::IDeviceCommandContext*,
	std::span<CModel*>)
{
}

void GPUSkinnedModelModelRenderer::PrepareModelDef(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	const CModelDef& def)
{
	m->modelDefRData = static_cast<ModelDefRData*>(def.GetRenderData(m.get()));
	ENSURE(m->modelDefRData);

	deviceCommandContext->SetVertexInputLayout(m->modelDefRData->m_VertexInputLayout);
	deviceCommandContext->SetIndexBuffer(m->modelDefRData->m_IndexArray.GetBuffer());

	if (m->modelDefRData->m_UVArray.GetStride() > 0)
	{
		const uint32_t firstVertexOffset{
			m->modelDefRData->m_UVArray.GetOffset() * m->modelDefRData->m_UVArray.GetStride()};
		deviceCommandContext->SetVertexBuffer(
			2, m->modelDefRData->m_UVArray.GetBuffer(), firstVertexOffset);
	}
}

void GPUSkinnedModelModelRenderer::RenderModels(
	Renderer::Backend::IDeviceCommandContext*, Renderer::Backend::IShaderProgram*,
	const std::span<CModel* const>)
{
	debug_warn("RenderModels called on a renderer without instancing support");
}

void GPUSkinnedModelModelRenderer::RenderModel(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext, Renderer::Backend::IShaderProgram*,
	CModel* model, CModelRData* data)
{
	ModelRData* modelRData{static_cast<ModelRData*>(data)};

	// Render the lot.
	const size_t numberOfFaces{model->GetModelDef()->GetNumFaces()};

	ModelRenderer::BindModelTransform(deviceCommandContext, *model);

	deviceCommandContext->SetVertexBuffer(
		0, modelRData->m_PositionHandle->m_Owner->GetBuffer(),
		modelRData->m_PositionHandle->m_Index * OUTPUT_POSITION_STRIDE);
	deviceCommandContext->SetVertexBuffer(
		1, modelRData->m_NormalTangentHandle->m_Owner->GetBuffer(),
		modelRData->m_NormalTangentHandle->m_Index * OUTPUT_NORMAL_TANGENT_STRIDE);

	deviceCommandContext->DrawIndexed(
		m->modelDefRData->m_IndexArray.GetOffset(), numberOfFaces * 3, 0);

	// Bump stats.
	g_Renderer.m_Stats.m_DrawCalls++;
	g_Renderer.m_Stats.m_ModelTris += numberOfFaces;
}
