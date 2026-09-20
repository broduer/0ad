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

#include "InstancingModelRenderer.h"

#include "graphics/MeshManager.h"
#include "graphics/Model.h"
#include "graphics/ModelDef.h"
#include "lib/debug.h"
#include "maths/Vector3D.h"
#include "maths/Vector4D.h"
#include "ps/containers/StaticVector.h"
#include "renderer/ModelRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/VertexArray.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/IShaderProgram.h"
#include "third_party/mikktspace/weldmesh.h"

#include <algorithm>
#include <iterator>
#include <cstddef>
#include <cstdint>
#include <vector>

struct IModelDef : public CModelDefRPrivate
{
	/// Static per-CModel vertex array
	VertexArray m_Array;

	/// Position and normals are static
	VertexArray::Attribute m_Position;
	VertexArray::Attribute m_Normal;
	VertexArray::Attribute m_Tangent;

	/// The number of UVs is determined by the model
	std::vector<VertexArray::Attribute> m_UVs;

	Renderer::Backend::IVertexInputLayout* m_VertexInputLayout = nullptr;

	/// Indices are the same for all models, so share them
	VertexIndexArray m_IndexArray;

	IModelDef(const CModelDefPtr& mdef);
};


IModelDef::IModelDef(const CModelDefPtr& mdef)
	: m_IndexArray(Renderer::Backend::IBuffer::Usage::TRANSFER_DST),
	m_Array(Renderer::Backend::IBuffer::Type::VERTEX, Renderer::Backend::IBuffer::Usage::TRANSFER_DST)
{
	size_t numVertices = mdef->GetNumVertices();

	m_Position.format = Renderer::Backend::Format::R32G32B32_SFLOAT;
	m_Array.AddAttribute(&m_Position);

	m_Normal.format = Renderer::Backend::Format::R32G32B32_SFLOAT;
	m_Array.AddAttribute(&m_Normal);

	m_UVs.resize(mdef->GetNumUVsPerVertex());
	for (size_t i = 0; i < mdef->GetNumUVsPerVertex(); i++)
	{
		m_UVs[i].format = Renderer::Backend::Format::R32G32_SFLOAT;
		m_Array.AddAttribute(&m_UVs[i]);
	}

	// Generate tangents for the geometry:-

	m_Tangent.format = Renderer::Backend::Format::R32G32B32A32_SFLOAT;
	m_Array.AddAttribute(&m_Tangent);

	// floats per vertex; position + normal + tangent + UV*sets
	int numVertexAttrs = 3 + 3 + 4 + 2 * mdef->GetNumUVsPerVertex();

	// the tangent generation can increase the number of vertices temporarily
	// so reserve a bit more memory to avoid reallocations in GenTangents (in most cases)
	std::vector<float> newVertices;
	newVertices.reserve(numVertexAttrs * numVertices * 2);

	// Generate the tangents
	ModelRenderer::GenTangents(mdef, newVertices, false);

	// how many vertices do we have after generating tangents?
	int newNumVert = newVertices.size() / numVertexAttrs;

	std::vector<int> remapTable(newNumVert);
	std::vector<float> vertexDataOut(newNumVert * numVertexAttrs);

	// re-weld the mesh to remove duplicated vertices
	int numVertices2 = WeldMesh(&remapTable[0], &vertexDataOut[0],
				&newVertices[0], newNumVert, numVertexAttrs);

	// Copy the model data to graphics memory:-

	m_Array.SetNumberOfVertices(numVertices2);
	m_Array.Layout();

	VertexArrayIterator<CVector3D> Position = m_Position.GetIterator<CVector3D>();
	VertexArrayIterator<CVector3D> Normal = m_Normal.GetIterator<CVector3D>();
	VertexArrayIterator<CVector4D> Tangent = m_Tangent.GetIterator<CVector4D>();

	// copy everything into the vertex array
	for (int i = 0; i < numVertices2; i++)
	{
		int q = numVertexAttrs * i;

		Position[i] = CVector3D(vertexDataOut[q + 0], vertexDataOut[q + 1], vertexDataOut[q + 2]);
		q += 3;

		Normal[i] = CVector3D(vertexDataOut[q + 0], vertexDataOut[q + 1], vertexDataOut[q + 2]);
		q += 3;

		Tangent[i] = CVector4D(vertexDataOut[q + 0], vertexDataOut[q + 1], vertexDataOut[q + 2],
				vertexDataOut[q + 3]);
		q += 4;

		for (size_t j = 0; j < mdef->GetNumUVsPerVertex(); j++)
		{
			VertexArrayIterator<float[2]> UVit = m_UVs[j].GetIterator<float[2]>();
			UVit[i][0] = vertexDataOut[q + 0 + 2 * j];
			UVit[i][1] = vertexDataOut[q + 1 + 2 * j];
		}
	}

	// upload vertex data
	m_Array.Upload();
	m_Array.FreeBackingStore();

	m_IndexArray.SetNumberOfVertices(mdef->GetNumFaces() * 3);
	m_IndexArray.Layout();

	VertexArrayIterator<std::uint16_t> Indices = m_IndexArray.GetIterator();

	size_t idxidx = 0;

	// reindex geometry and upload index
	for (size_t j = 0; j < mdef->GetNumFaces(); ++j)
	{
		Indices[idxidx++] = remapTable[j * 3 + 0];
		Indices[idxidx++] = remapTable[j * 3 + 1];
		Indices[idxidx++] = remapTable[j * 3 + 2];
	}

	m_IndexArray.Upload();
	m_IndexArray.FreeBackingStore();

	const uint32_t stride = m_Array.GetStride();
	constexpr size_t MAX_UV = 2;

	PS::StaticVector<Renderer::Backend::SVertexAttributeFormat, 4 + 5 + MAX_UV> attributes{
		{Renderer::Backend::VertexAttributeStream::POSITION,
			m_Position.format, m_Position.offset, stride,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0},
		{Renderer::Backend::VertexAttributeStream::NORMAL,
			m_Normal.format, m_Normal.offset, stride,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0}
	};

	for (size_t uv = 0; uv < std::min(MAX_UV, mdef->GetNumUVsPerVertex()); ++uv)
	{
		const Renderer::Backend::VertexAttributeStream stream =
			static_cast<Renderer::Backend::VertexAttributeStream>(
				static_cast<int>(Renderer::Backend::VertexAttributeStream::UV0) + uv);
		attributes.push_back({
			stream, m_UVs[uv].format, m_UVs[uv].offset, stride,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0});
	}

	attributes.push_back({
		Renderer::Backend::VertexAttributeStream::UV2,
		m_Tangent.format, m_Tangent.offset, stride,
		Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0});

	std::ranges::copy(ModelRenderer::GetInstanceTransformAttributes(), std::back_inserter(attributes));

	m_VertexInputLayout = g_Renderer.GetVertexInputLayout({attributes.begin(), attributes.end()});
}

struct InstancingModelRendererInternals
{
	/// Previously prepared modeldef
	IModelDef* imodeldef;
	std::vector<float> transforms;
};


// Construction and Destruction
InstancingModelRenderer::InstancingModelRenderer()
{
	m = new InstancingModelRendererInternals;
	m->imodeldef = 0;
}

InstancingModelRenderer::~InstancingModelRenderer()
{
	delete m;
}


// Build modeldef data if necessary - we have no per-CModel data
CModelRData* InstancingModelRenderer::CreateModelData(const void* key, CModel* model)
{
	CModelDefPtr mdef = model->GetModelDef();
	IModelDef* imodeldef = (IModelDef*)mdef->GetRenderData(m);

	ENSURE(!model->IsSkinned());

	if (!imodeldef)
	{
		imodeldef = new IModelDef(mdef);
		mdef->SetRenderData(m, imodeldef);
	}

	return new CModelRData(key);
}

void InstancingModelRenderer::UpdateModelsData(Renderer::Backend::IDeviceCommandContext*,
	std::span<CModel*>)
{
	// We have no per-CModel data
}

void InstancingModelRenderer::UploadModelsData(Renderer::Backend::IDeviceCommandContext*,
	std::span<CModel*>)
{
	// Data uploaded once during creation as we don't update it dynamically.
}

// Prepare UV coordinates for this modeldef
void InstancingModelRenderer::PrepareModelDef(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	const CModelDef& def)
{
	m->imodeldef = (IModelDef*)def.GetRenderData(m);
	ENSURE(m->imodeldef);

	deviceCommandContext->SetVertexInputLayout(m->imodeldef->m_VertexInputLayout);

	deviceCommandContext->SetIndexBuffer(m->imodeldef->m_IndexArray.GetBuffer());

	const uint32_t stride = m->imodeldef->m_Array.GetStride();
	const uint32_t firstVertexOffset = m->imodeldef->m_Array.GetOffset() * stride;

	deviceCommandContext->SetVertexBuffer(
		0, m->imodeldef->m_Array.GetBuffer(), firstVertexOffset);
}


// Render models sharing the prepared modeldef
void InstancingModelRenderer::RenderModels(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	Renderer::Backend::IShaderProgram*, const std::span<CModel* const> models)
{
	if (models.empty())
		return;
	std::vector<float>& transforms = m->transforms;
	transforms.clear();
	transforms.reserve(models.size() * 16);
	for (CModel* model : models)
	{
		const std::span<const float, 16> matrix = model->GetTransform().AsFloatArray();
		transforms.insert(transforms.end(), matrix.begin(), matrix.end());
	}
	deviceCommandContext->SetVertexBufferData(
		MODEL_INSTANCE_BINDING_SLOT, transforms.data(),
		static_cast<uint32_t>(transforms.size() * sizeof(float)));

	const size_t numberOfFaces = models[0]->GetModelDef()->GetNumFaces();
	deviceCommandContext->DrawIndexedInstanced(
		m->imodeldef->m_IndexArray.GetOffset(), numberOfFaces * 3, 0,
		static_cast<uint32_t>(models.size()), 0);

	g_Renderer.m_Stats.m_DrawCalls++;
	g_Renderer.m_Stats.m_ModelTris += numberOfFaces * models.size();
}

// Render one model
void InstancingModelRenderer::RenderModel(Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	Renderer::Backend::IShaderProgram*, CModel* model, CModelRData*)
{
	const CModelDefPtr& mdldef = model->GetModelDef();

	ModelRenderer::BindModelTransform(deviceCommandContext, *model);

	// Render the lot.
	const size_t numberOfFaces = mdldef->GetNumFaces();

	deviceCommandContext->DrawIndexedInRange(
		m->imodeldef->m_IndexArray.GetOffset(), numberOfFaces * 3, 0, m->imodeldef->m_Array.GetNumberOfVertices() - 1);

	// Bump stats.
	g_Renderer.m_Stats.m_DrawCalls++;
	g_Renderer.m_Stats.m_ModelTris += numberOfFaces;
}
