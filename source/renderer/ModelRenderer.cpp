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

#include "graphics/Camera.h"
#include "graphics/Color.h"
#include "graphics/LightEnv.h"
#include "graphics/Material.h"
#include "graphics/Model.h"
#include "graphics/ModelDef.h"
#include "graphics/SColor.h"
#include "graphics/ShaderDefines.h"
#include "graphics/ShaderManager.h"
#include "graphics/ShaderTechnique.h"
#include "graphics/ShaderTechniquePtr.h"
#include "graphics/Texture.h"
#include "graphics/TextureManager.h"
#include "lib/alignment.h"
#include "lib/allocators/STLAllocators.h"
#include "lib/debug.h"
#include "lib/hash.h"
#include "maths/Matrix3D.h"
#include "maths/Vector2D.h"
#include "maths/Vector3D.h"
#include "ps/CLogger.h"
#include "ps/CStrIntern.h"
#include "ps/CStrInternStatic.h"
#include "ps/containers/StaticVector.h"
#include "ps/memory/LinearAllocator.h"
#include "ps/Profile.h"
#include "renderer/MikktspaceWrap.h"
#include "renderer/ModelRenderer.h"
#include "renderer/ModelVertexRenderer.h"
#include "renderer/RenderModifiers.h"
#include "renderer/Renderer.h"
#include "renderer/SceneRenderer.h"
#include "renderer/SkyManager.h"
#include "renderer/TimeManager.h"
#include "renderer/VertexArray.h"
#include "renderer/WaterManager.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/IShaderProgram.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>


namespace
{

CMaterial::Pass GetMaterialPassFromCullGroup(const int cullGroup, const ERenderMode renderMode)
{
	switch (renderMode)
	{
	case ERenderMode::WIREFRAME:
		return CMaterial::Pass::WIREFRAME;
	case ERenderMode::EDGED_FACES:
		return CMaterial::Pass::WIREFRAME_SOLID;
	case ERenderMode::SOLID:
		break;
	}

	switch (cullGroup)
	{
	case CSceneRenderer::CULL_SHADOWS_CASCADE_0: [[fallthrough]];
	case CSceneRenderer::CULL_SHADOWS_CASCADE_1: [[fallthrough]];
	case CSceneRenderer::CULL_SHADOWS_CASCADE_2: [[fallthrough]];
	case CSceneRenderer::CULL_SHADOWS_CASCADE_3:
		return CMaterial::Pass::SHADOW_CASTER;
	case CSceneRenderer::CULL_SILHOUETTE_OCCLUDER:
		return CMaterial::Pass::SILHOUETTE_OCCLUDER;
	case CSceneRenderer::CULL_SILHOUETTE_CASTER:
		return CMaterial::Pass::SILHOUETTE_CASTER;
	case CSceneRenderer::CULL_DEFAULT:
		return CMaterial::Pass::MAIN;
	default:
		break;
	}

	return CMaterial::Pass::MAIN;
}

}

// static
void ModelRenderer::CopyPositionAndNormals(
	const CModelDefPtr& mdef,
	const VertexArrayIterator<CVector3D>& Position,
	const VertexArrayIterator<CVector3D>& Normal)
{
	size_t numVertices = mdef->GetNumVertices();
	SModelVertex* vertices = mdef->GetVertices();

	for (size_t j = 0; j < numVertices; ++j)
	{
		Position[j] = vertices[j].m_Coords;
		Normal[j] = vertices[j].m_Norm;
	}
}

// static
void ModelRenderer::BuildPositionAndNormals(
	CModel* model,
	const VertexArrayIterator<CVector3D>& Position,
	const VertexArrayIterator<CVector3D>& Normal)
{
	CModelDefPtr mdef = model->GetModelDef();
	size_t numVertices = mdef->GetNumVertices();
	SModelVertex* vertices = mdef->GetVertices();

	if (model->IsSkinned())
	{
		// boned model - calculate skinned vertex positions/normals

		// Avoid the noisy warnings that occur inside SkinPoint/SkinNormal in
		// some broken situations
		if (numVertices && vertices[0].m_Blend.m_Bone[0] == 0xff)
		{
			LOGERROR("Model %s is boned with unboned animation", mdef->GetName().string8());
			return;
		}

		CModelDef::SkinPointsAndNormals(numVertices, Position, Normal, vertices, mdef->GetBlendIndices(), model->GetAnimatedBoneMatrices());
	}
	else
	{
		PROFILE("software transform");
		// just copy regular positions, transform normals to world space
		const CMatrix3D& transform = model->GetTransform();
		const CMatrix3D& invtransform = model->GetInvTransform();
		for (size_t j = 0; j < numVertices; ++j)
		{
			transform.Transform(vertices[j].m_Coords, Position[j]);
			invtransform.RotateTransposed(vertices[j].m_Norm, Normal[j]);
		}
	}
}

// static
void ModelRenderer::GenTangents(const CModelDefPtr& mdef, std::vector<float>& newVertices, bool gpuSkinning)
{
	MikkTSpace ms(mdef, newVertices, gpuSkinning);
	ms.Generate();
}

// static
void ModelRenderer::BuildUV(
	const CModelDefPtr& mdef, const VertexArrayIterator<float[2]>& UV, int UVset)
{
	const size_t numVertices = mdef->GetNumVertices();
	const size_t numberOfUVPerVertex = mdef->GetNumUVsPerVertex();

	for (size_t j = 0; j < numVertices; ++j)
	{
		const CVector2D& uv = mdef->GetUVCoordinates()[j * numberOfUVPerVertex + UVset];
		UV[j][0] = uv.X;
		UV[j][1] = 1.0 - uv.Y;
	}
}

// static
void ModelRenderer::BuildIndices(
	const CModelDefPtr& mdef, const VertexArrayIterator<std::uint16_t>& Indices)
{
	size_t idxidx = 0;
	SModelFace* faces = mdef->GetFaces();

	for (size_t j = 0; j < mdef->GetNumFaces(); ++j)
	{
		SModelFace& face = faces[j];
		Indices[idxidx++] = face.m_Verts[0];
		Indices[idxidx++] = face.m_Verts[1];
		Indices[idxidx++] = face.m_Verts[2];
	}
}

// Helper structs for ShaderModelRenderer::Render():

struct SMRSortByDistItem
{
	size_t techIdx;
	CModel* model;
	float dist;
};

struct SMRBatchKey
{
	const CModelDef* modelDef;
	const CTexture* texture;
	// Interned static uniforms.
	const void* uniforms;

	auto operator<=>(const SMRBatchKey&) const = default;
};

/**
 * A model with its batch key, copied so sorting doesn't dereference the model.
 */
struct SMRBatchItem
{
	SMRBatchKey key;
	CModel* model;
	uint32_t group;

	static SMRBatchItem Of(CModel* model)
	{
		const CMaterial& material = model->GetMaterial();
		return {{model->GetModelDef().get(), material.GetDiffuseTexture().get(),
			material.GetStaticUniforms().GetInterned()}, model, 0};
	}
};

struct SMRBatchGroup
{
	SMRBatchKey key;
	uint32_t count;
	uint32_t start;
};

constexpr size_t MAX_BATCH_GROUPS = 32;
using SMRBatchGroups = PS::StaticVector<SMRBatchGroup, MAX_BATCH_GROUPS>;

/**
 * Group items by key, or return nothing if there are too many keys.
 */
std::optional<SMRBatchGroups> GroupBatchItems(const std::span<SMRBatchItem> items)
{
	SMRBatchGroups groups;
	for (SMRBatchItem& item : items)
	{
		SMRBatchGroup* group = std::ranges::find(groups, item.key, &SMRBatchGroup::key);
		if (group == groups.end())
		{
			if (groups.full())
				return std::nullopt;
			group = &groups.emplace_back(item.key, 0u, 0u);
		}
		++group->count;
		item.group = static_cast<uint32_t>(group - groups.begin());
	}
	return groups;
}

/**
 * Sort models by batch key.
 */
template<typename Models, typename Items, typename Ordered>
void SortModelsForBatching(Models& models, Items& items, Ordered& ordered)
{
	if (models.size() < 2)
		return;
	items.clear();
	items.reserve(models.size());
	std::ranges::transform(models, std::back_inserter(items), &SMRBatchItem::Of);

	std::optional<SMRBatchGroups> grouped{GroupBatchItems(items)};
	if (!grouped)
	{
		std::ranges::sort(items, {}, &SMRBatchItem::key);
		std::ranges::transform(items, models.begin(), &SMRBatchItem::model);
		return;
	}
	SMRBatchGroups& groups{*grouped};
	if (groups.size() < 2)
		return;

	PS::StaticVector<uint32_t, MAX_BATCH_GROUPS> order;
	order.resize(groups.size());
	std::iota(order.begin(), order.end(), 0u);
	std::ranges::sort(order, {},
		[&groups](const uint32_t group) -> const SMRBatchKey& { return groups[group].key; });
	std::uint32_t start{0};
	for (const std::uint32_t group : order)
	{
		groups[group].start = start;
		start += groups[group].count;
	}

	ordered.resize(models.size());
	for (const SMRBatchItem& item : items)
		ordered[groups[item.group].start++] = item.model;
	std::ranges::copy(ordered, models.begin());
}

struct SMRCompareSortByDistItem
{
	bool operator()(const SMRSortByDistItem& a, const SMRSortByDistItem& b)
	{
		// Prefer items with greater distance, so we draw back-to-front
		return (a.dist > b.dist);

		// (Distances will almost always be distinct, so we don't need to bother
		// tie-breaking on modeldef/texture/etc)
	}
};

class SMRMaterialBucketKey
{
public:
	SMRMaterialBucketKey(CStrIntern effect, const CShaderDefines& defines)
		: effect(effect), defines(defines) { }

	SMRMaterialBucketKey(const SMRMaterialBucketKey& entity) = default;

	CStrIntern effect;
	CShaderDefines defines;

	bool operator==(const SMRMaterialBucketKey& b) const
	{
		return (effect == b.effect && defines == b.defines);
	}

private:
	SMRMaterialBucketKey& operator=(const SMRMaterialBucketKey&);
};

struct SMRMaterialBucketKeyHash
{
	size_t operator()(const SMRMaterialBucketKey& key) const
	{
		size_t hash = 0;
		hash_combine(hash, key.effect.GetHash());
		hash_combine(hash, key.defines.GetHash());
		return hash;
	}
};

struct SMRTechBucket
{
	CShaderTechniquePtr tech;
	std::span<CModel*> models;

	// Model list is stored as pointers, not as a std::vector,
	// so that sorting lists of this struct is fast
};

struct SMRCompareTechBucket
{
	bool operator()(const SMRTechBucket& a, const SMRTechBucket& b)
	{
		return a.tech < b.tech;
	}
};

std::array<Renderer::Backend::SVertexAttributeFormat, 4> ModelRenderer::GetInstanceTransformAttributes()
{
	const auto row = [](const uint32_t index)
	{
		return Renderer::Backend::SVertexAttributeFormat{
			static_cast<Renderer::Backend::VertexAttributeStream>(
				static_cast<uint32_t>(Renderer::Backend::VertexAttributeStream::UV3) + index),
			Renderer::Backend::Format::R32G32B32A32_SFLOAT, index * 16, MODEL_INSTANCE_STRIDE,
			Renderer::Backend::VertexAttributeRate::PER_INSTANCE, MODEL_INSTANCE_BINDING_SLOT};
	};
	return {row(0), row(1), row(2), row(3)};
}

void ModelRenderer::BindModelTransform(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const CModel& model)
{
	deviceCommandContext->SetVertexBufferData(
		MODEL_INSTANCE_BINDING_SLOT, model.GetTransform().AsFloatArray().data(), MODEL_INSTANCE_STRIDE);
}

void ModelRenderer::Render(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	ModelVertexRenderer& modelVertexRenderer, RenderModifier& modifier, const CShaderDefines& context,
	int cullGroup, int flags, const ERenderMode renderMode, std::span<CModel*> submissions)
{
	if (submissions.empty())
		return;

	CMatrix3D worldToCam;
	g_Renderer.GetSceneRenderer().GetViewCamera().GetOrientation().GetInverse(worldToCam);

	const CMaterial::Pass materialPass{GetMaterialPassFromCullGroup(cullGroup, renderMode)};

	/*
	 * Rendering approach:
	 *
	 * submissions contains the list of CModels to render.
	 *
	 * The data we need to render a model is:
	 *  - CShaderTechnique
	 *  - CTexture
	 *  - CShaderUniforms
	 *  - CModelDef (mesh data)
	 *  - CModel (model instance data)
	 *
	 * For efficient rendering, we need to batch the draw calls to minimise state changes.
	 * (Uniform and texture changes are assumed to be cheaper than binding new mesh data,
	 * and shader changes are assumed to be most expensive.)
	 * First, group all models that share a technique to render them together.
	 * Within those groups, sub-group by CModelDef.
	 * Within those sub-groups, sub-sub-group by CTexture.
	 * Within those sub-sub-groups, sub-sub-sub-group by CShaderUniforms.
	 *
	 * Alpha-blended models have to be sorted by distance from camera,
	 * then we can batch as long as the order is preserved.
	 * Non-alpha-blended models can be arbitrarily reordered to maximise batching.
	 *
	 * For each model, the CShaderTechnique is derived from:
	 *  - The current global 'context' defines
	 *  - The CModel's material's defines
	 *  - The CModel's material's shader effect name
	 *
	 * There are a smallish number of materials, and a smaller number of techniques.
	 *
	 * To minimise technique lookups, we first group models by material,
	 * in 'materialBuckets' (a hash table).
	 *
	 * For each material bucket we then look up the appropriate shader technique.
	 * If the technique requires sort-by-distance, the model is added to the
	 * 'sortByDistItems' list with its computed distance.
	 * Otherwise, the bucket's list of models is sorted by modeldef+texture+uniforms,
	 * then the technique and model list is added to 'techBuckets'.
	 *
	 * 'techBuckets' is then sorted by technique, to improve batching when multiple
	 * materials map onto the same technique.
	 *
	 * (Note that this isn't perfect batching: we don't sort across models in
	 * multiple buckets that share a technique. In practice that shouldn't reduce
	 * batching much (we rarely have one mesh used with multiple materials),
	 * and it saves on copying and lets us sort smaller lists.)
	 *
	 * Extra tech buckets are added for the sorted-by-distance models without reordering.
	 * Finally we render by looping over each tech bucket, then looping over the model
	 * list in each, rebinding the GL state whenever it changes.
	 */

	PS::Memory::ScopedLinearAllocator scopedLinearAllocator{g_Renderer.GetLinearAllocator()};

	using ModelListAllocator = ProxyAllocator<CModel*, PS::Memory::ScopedLinearAllocator>;
	using ModelList_t = std::vector<CModel*, ModelListAllocator>;
	using MaterialBuckets_t = std::unordered_map<
		SMRMaterialBucketKey,
		ModelList_t,
		SMRMaterialBucketKeyHash,
		std::equal_to<SMRMaterialBucketKey>,
		ProxyAllocator<
			std::pair<const SMRMaterialBucketKey, ModelList_t>,
			PS::Memory::ScopedLinearAllocator>>;

	MaterialBuckets_t materialBuckets((MaterialBuckets_t::allocator_type(scopedLinearAllocator)));

	{
		PROFILE3("bucketing by material");

		for (CModel* model : submissions)
		{
			const CMaterial& material{model->GetMaterial()};
			const CShaderDefines& defines{material.GetShaderDefines()};
			const CStrIntern shaderEffect{material.GetShaderEffect(materialPass)};
			SMRMaterialBucketKey key(shaderEffect, defines);

			MaterialBuckets_t::iterator it = materialBuckets.find(key);
			if (it == materialBuckets.end())
			{
				std::pair<MaterialBuckets_t::iterator, bool> inserted = materialBuckets.insert(
					std::make_pair(key, ModelList_t(ModelList_t::allocator_type(scopedLinearAllocator))));
				inserted.first->second.reserve(32);
				inserted.first->second.push_back(model);
			}
			else
			{
				it->second.push_back(model);
			}
		}
	}

	using SortByDistItemsAllocator = ProxyAllocator<SMRSortByDistItem, PS::Memory::ScopedLinearAllocator>;
	std::vector<SMRSortByDistItem, SortByDistItemsAllocator> sortByDistItems((SortByDistItemsAllocator(scopedLinearAllocator)));

	using SortByTechItemsAllocator = ProxyAllocator<CShaderTechniquePtr, PS::Memory::ScopedLinearAllocator>;
	std::vector<CShaderTechniquePtr, SortByTechItemsAllocator> sortByDistTechs((SortByTechItemsAllocator(scopedLinearAllocator)));
		// indexed by sortByDistItems[i].techIdx
		// (which stores indexes instead of CShaderTechniquePtr directly
		// to avoid the shared_ptr copy cost when sorting; maybe it'd be better
		// if we just stored raw CShaderTechnique* and assumed the shader manager
		// will keep it alive long enough)

	using TechBucketsAllocator =  ProxyAllocator<SMRTechBucket, PS::Memory::ScopedLinearAllocator>;
	std::vector<SMRTechBucket, TechBucketsAllocator> techBuckets((TechBucketsAllocator(scopedLinearAllocator)));

	using BatchItemsAllocator = ProxyAllocator<SMRBatchItem, PS::Memory::ScopedLinearAllocator>;
	std::vector<SMRBatchItem, BatchItemsAllocator> batchItems((BatchItemsAllocator(scopedLinearAllocator)));
	std::vector<CModel*, ModelListAllocator> batchOrdered((ModelListAllocator(scopedLinearAllocator)));
	const bool supportsInstancing = deviceCommandContext->GetDevice()->GetCapabilities().instancing;

	{
		PROFILE3("processing material buckets");
		for (MaterialBuckets_t::iterator it = materialBuckets.begin(); it != materialBuckets.end(); ++it)
		{
			CShaderDefines defines = context;
			defines.SetMany(it->first.defines);
			CShaderTechniquePtr tech = g_Renderer.GetShaderManager().LoadEffect(it->first.effect, defines);

			// Skip invalid techniques (e.g. from data file errors)
			if (!tech)
				continue;

			if (tech->GetSortByDistance())
			{
				// Add the tech into a vector so we can index it
				// (There might be duplicates in this list, but that doesn't really matter)
				if (sortByDistTechs.empty() || sortByDistTechs.back() != tech)
					sortByDistTechs.push_back(tech);
				size_t techIdx = sortByDistTechs.size() - 1;

				// Add each model into sortByDistItems
				for (size_t i = 0; i < it->second.size(); ++i)
				{
					SMRSortByDistItem itemWithDist;
					itemWithDist.techIdx = techIdx;

					CModel* model = it->second[i];
					itemWithDist.model = model;

					CVector3D modelpos = model->GetTransform().GetTranslation();
					itemWithDist.dist = worldToCam.Transform(modelpos).Z;

					sortByDistItems.push_back(itemWithDist);
				}
			}
			else
			{
				// Sort model list by modeldef+texture, for batching
				// TODO: This only sorts by base texture. While this is an OK approximation
				// for most cases (as related samplers are usually used together), it would be better
				// to take all the samplers into account when sorting here.
				SortModelsForBatching(it->second, batchItems, batchOrdered);

				// Add a tech bucket pointing at this model list
				SMRTechBucket techBucket{tech, it->second};
				techBuckets.push_back(techBucket);
			}
		}
	}

	{
		PROFILE3("sorting tech buckets");
		// Sort by technique, for better batching
		std::sort(techBuckets.begin(), techBuckets.end(), SMRCompareTechBucket());
	}

	// List of models corresponding to sortByDistItems[i].model
	// (This exists primarily because techBuckets wants a CModel**;
	// we could avoid the cost of copying into this list by adding
	// a stride length into techBuckets and not requiring contiguous CModel*s)
	std::vector<CModel*, ModelListAllocator> sortByDistModels((ModelListAllocator(scopedLinearAllocator)));

	if (!sortByDistItems.empty())
	{
		{
			PROFILE3("sorting items by dist");
			std::sort(sortByDistItems.begin(), sortByDistItems.end(), SMRCompareSortByDistItem());
		}

		{
			PROFILE3("batching dist-sorted items");

			sortByDistModels.reserve(sortByDistItems.size());

			// Find runs of distance-sorted models that share a technique,
			// and create a new tech bucket for each run

			size_t start = 0; // start of current run
			size_t currentTechIdx = sortByDistItems[start].techIdx;

			for (size_t end = 0; end < sortByDistItems.size(); ++end)
			{
				sortByDistModels.push_back(sortByDistItems[end].model);

				size_t techIdx = sortByDistItems[end].techIdx;
				if (techIdx != currentTechIdx)
				{
					// Start of a new run - push the old run into a new tech bucket
					SMRTechBucket techBucket{sortByDistTechs[currentTechIdx],
						{sortByDistModels.data() + start, end - start}};
					techBuckets.push_back(techBucket);
					start = end;
					currentTechIdx = techIdx;
				}
			}

			// Add the tech bucket for the final run
			SMRTechBucket techBucket{sortByDistTechs[currentTechIdx],
				{sortByDistModels.data() + start, sortByDistItems.size() - start}};
			techBuckets.push_back(techBucket);
		}
	}

	const double time = g_Renderer.GetTimeManager().GetGlobalTime();

	{
		PROFILE3("rendering bucketed submissions");

		size_t idxTechStart = 0;

		// This vector keeps track of texture changes during rendering. It is kept outside the
		// loops to avoid excessive reallocations. The token allocation of 64 elements
		// should be plenty, though it is reallocated below (at a cost) if necessary.
		using TextureListAllocator = ProxyAllocator<CTexture*, PS::Memory::ScopedLinearAllocator>;
		std::vector<CTexture*, TextureListAllocator> currentTexs((TextureListAllocator(scopedLinearAllocator)));
		currentTexs.reserve(64);

		// texBindings holds the identifier bindings in the shader, which can no longer be defined
		// statically in the ShaderRenderModifier class. texBindingNames uses interned strings to
		// keep track of when bindings need to be reevaluated.
		using BindingListAllocator = ProxyAllocator<int32_t, PS::Memory::ScopedLinearAllocator>;
		std::vector<int32_t, BindingListAllocator> texBindings((BindingListAllocator(scopedLinearAllocator)));
		texBindings.reserve(64);

		using BindingNamesListAllocator = ProxyAllocator<CStrIntern, PS::Memory::ScopedLinearAllocator>;
		std::vector<CStrIntern, BindingNamesListAllocator> texBindingNames((BindingNamesListAllocator(scopedLinearAllocator)));
		texBindingNames.reserve(64);

		while (idxTechStart < techBuckets.size())
		{
			CShaderTechniquePtr currentTech = techBuckets[idxTechStart].tech;

			// Find runs [idxTechStart, idxTechEnd) in techBuckets of the same technique
			size_t idxTechEnd;
			for (idxTechEnd = idxTechStart + 1; idxTechEnd < techBuckets.size(); ++idxTechEnd)
			{
				if (techBuckets[idxTechEnd].tech != currentTech)
					break;
			}

			// For each of the technique's passes, render all the models in this run
			for (int pass = 0; pass < currentTech->GetNumPasses(); ++pass)
			{
				deviceCommandContext->SetGraphicsPipelineState(
					currentTech->GetGraphicsPipelineState(pass));
				deviceCommandContext->BeginPass();

				Renderer::Backend::IShaderProgram* shader = currentTech->GetShader(pass);

				modifier.BeginPass(deviceCommandContext, shader);

				// TODO: Use a more generic approach to handle bound queries.
				bool boundTime = false;
				bool boundWaterTexture = false;
				bool boundSkyCube = false;

				// When the shader technique changes, textures need to be
				// rebound, so ensure there are no remnants from the last pass.
				// (the vector size is set to 0, but memory is not freed)
				currentTexs.clear();
				texBindings.clear();
				texBindingNames.clear();

				CModelDef* currentModeldef = NULL;
				CShaderUniforms currentStaticUniforms;

				// Accumulate models sharing the bound state, flushing on any state change.
				const bool instanced =
					modelVertexRenderer.CanInstance() && modifier.UsesInstanceAttributes() &&
					supportsInstancing;
				std::vector<CModel*, ModelListAllocator> run((ModelListAllocator(scopedLinearAllocator)));
				const auto flushRun = [&]()
				{
					if (run.empty())
						return;
					modelVertexRenderer.RenderModels(deviceCommandContext, shader, run);
					run.clear();
				};

				for (size_t idx = idxTechStart; idx < idxTechEnd; ++idx)
				{
					for (CModel* model : techBuckets[idx].models)
					{
						if (flags && !(model->GetFlags() & flags))
							continue;

						const CMaterial::SamplersVector& samplers = model->GetMaterial().GetSamplers();
						size_t samplersNum = samplers.size();

						// make sure the vectors are the right virtual sizes, and also
						// reallocate if there are more samplers than expected.
						if (currentTexs.size() != samplersNum)
						{
							currentTexs.resize(samplersNum, NULL);
							texBindings.resize(samplersNum, -1);
							texBindingNames.resize(samplersNum, CStrIntern());

							// ensure they are definitely empty
							std::fill(texBindings.begin(), texBindings.end(), -1);
							std::fill(currentTexs.begin(), currentTexs.end(), nullptr);
							std::fill(texBindingNames.begin(), texBindingNames.end(), CStrIntern());
						}

						// bind the samplers to the shader
						for (size_t s = 0; s < samplersNum; ++s)
						{
							const CMaterial::TextureSampler& samp = samplers[s];

							// check that the handles are current
							// and reevaluate them if necessary
							if (texBindingNames[s] != samp.Name || texBindings[s] < 0)
							{
								texBindings[s] = shader->GetBindingSlot(samp.Name);
								texBindingNames[s] = samp.Name;
							}

							// same with the actual sampler bindings
							CTexture* newTex = samp.Sampler.get();
							if (texBindings[s] >= 0 && newTex != currentTexs[s])
							{
								flushRun();
								newTex->UploadBackendTextureIfNeeded(deviceCommandContext);
								deviceCommandContext->SetTexture(
									texBindings[s], newTex->GetBackendTexture());
								currentTexs[s] = newTex;
							}
						}

						// Bind modeldef when it changes
						CModelDef* newModeldef = model->GetModelDef().get();
						if (newModeldef != currentModeldef)
						{
							flushRun();
							currentModeldef = newModeldef;
							modelVertexRenderer.PrepareModelDef(deviceCommandContext, *currentModeldef);
						}

						// Bind all uniforms when any change
						CShaderUniforms newStaticUniforms = model->GetMaterial().GetStaticUniforms();
						if (newStaticUniforms != currentStaticUniforms)
						{
							flushRun();
							currentStaticUniforms = newStaticUniforms;
							currentStaticUniforms.BindUniforms(deviceCommandContext, shader);
						}

						const CShaderRenderQueries& renderQueries = model->GetMaterial().GetRenderQueries();

						for (size_t q = 0; q < renderQueries.GetSize(); ++q)
						{
							CShaderRenderQueries::RenderQuery rq = renderQueries.GetItem(q);
							if (rq.first == RQUERY_TIME)
							{
								if (!boundTime)
								{
									flushRun();
									deviceCommandContext->SetUniform(
										shader->GetBindingSlot(rq.second), time);
									boundTime = true;
								}
							}
							else if (rq.first == RQUERY_WATER_TEX)
							{
								if (!boundWaterTexture)
								{
									flushRun();
									const double period = 1.6;
									const WaterManager& waterManager = g_Renderer.GetSceneRenderer().GetWaterManager();
									if (waterManager.m_RenderWater && waterManager.WillRenderFancyWater())
									{
										const CTexturePtr& waterTexture = waterManager.m_NormalMap[waterManager.GetCurrentTextureIndex(period)];
										waterTexture->UploadBackendTextureIfNeeded(deviceCommandContext);
										deviceCommandContext->SetTexture(
											shader->GetBindingSlot(str_waterTex),
											waterTexture->GetBackendTexture());
									}
									else
									{
										deviceCommandContext->SetTexture(
											shader->GetBindingSlot(str_waterTex),
											g_Renderer.GetTextureManager().GetErrorTexture()->GetBackendTexture());
									}
									boundWaterTexture = true;
								}
							}
							else if (rq.first == RQUERY_SKY_CUBE)
							{
								if (!boundSkyCube)
								{
									flushRun();
									deviceCommandContext->SetTexture(
										shader->GetBindingSlot(str_skyCube),
										g_Renderer.GetSceneRenderer().GetSkyManager().GetSkyCube());
									boundSkyCube = true;
								}
							}
						}

						if (modifier.WouldChangeModelState(model))
							flushRun();
						modifier.PrepareModel(deviceCommandContext, model);

						CModelRData* rdata = static_cast<CModelRData*>(model->GetRenderData());
						ENSURE(rdata->GetKey() == &modelVertexRenderer);

						if (instanced)
							run.push_back(model);
						else
							modelVertexRenderer.RenderModel(deviceCommandContext, shader, model, rdata);
					}
				}
				flushRun();

				deviceCommandContext->EndPass();
			}

			idxTechStart = idxTechEnd;
		}
	}
}
