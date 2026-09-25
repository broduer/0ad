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

/*
 * Home to the ModelRenderer class, an abstract base class that manages
 * a per-frame list of submitted models, as well as simple helper
 * classes.
 */

#ifndef INCLUDED_MODELRENDERER
#define INCLUDED_MODELRENDERER

#include "graphics/MeshManager.h"
#include "graphics/RenderableObject.h"
#include "renderer/backend/IShaderProgram.h"
#include "renderer/SceneRenderer.h"

#include <array>
#include <memory>
#include <span>
#include <vector>

class CModel;
class CShaderDefines;
class CVector3D;
namespace Renderer::Backend { class IDeviceCommandContext; }
struct SColor4ub;
template <typename T> class VertexArrayIterator;

class ModelVertexRenderer;
class RenderModifier;

/**
 * Class CModelRData: Render data that is maintained per CModel.
 * ModelRenderer implementations may derive from this class to store
 * per-CModel data.
 *
 * The main purpose of this class over CRenderData is to track which
 * ModelRenderer the render data belongs to (via the key that is passed
 * to the constructor). When a model changes the renderer it uses
 * (e.g. via run-time modification of the gpu skinning configuration),
 * the old ModelRenderer's render data is supposed to be replaced by
 * the new data.
 */
class CModelRData : public CRenderData
{
public:
	CModelRData(const void* key) : m_Key(key) { }

	/**
	 * GetKey: Retrieve the key that can be used to identify the
	 * ModelRenderer that created this data.
	 *
	 * @return The opaque key that was passed to the constructor.
	 */
	const void* GetKey() const { return m_Key; }

private:
	/// The key for model renderer identification
	const void* m_Key;
};


/**
 * Binding slot of the instance transform.
 */
constexpr uint32_t MODEL_INSTANCE_BINDING_SLOT = 3;
constexpr uint32_t MODEL_INSTANCE_STRIDE = 64;


/**
 * ModelRenderer renders a per-frame list of models. It loads the appropriate
 * shaders for rendering each model, and that batches by shader technique (and
 * by mesh and texture).
 *
 * ModelRenderer delegates vertex transformation/setup to a
 * ModelVertexRenderer. It delegates fragment stage setup to a RenderModifier.
 *
 * ModelRenderer also contains a number of static helper functions
 * for building vertex arrays.
 */
class ModelRenderer
{
public:
	ModelRenderer();
	~ModelRenderer();

	/**
	 * Render: Render submitted models, using the given RenderModifier to setup
	 * the fragment stage.
	 *
	 * preconditions  : PrepareModels must be called after all models have been
	 * submitted and before calling Render.
	 *
	 * @param modifier The RenderModifier that specifies the fragment stage.
	 * @param flags If flags is 0, all submitted models are rendered.
	 * If flags is non-zero, only models that contain flags in their
	 * CModel::GetFlags() are rendered.
	 */
	void Render(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		ModelVertexRenderer& modelVertexRenderer, RenderModifier& modifier, const CShaderDefines& context,
		int cullGroup, int flags, const ERenderMode renderMode, std::span<CModel*> submissions);

	/**
	 * CopyPositionAndNormals: Copy unanimated object-space vertices and
	 * normals into the given vertex array.
	 *
	 * @param mdef The underlying CModelDef that contains mesh data.
	 * @param Position Points to the array that will receive
	 * position vectors. The array behind the iterator
	 * must be large enough to hold model->GetModelDef()->GetNumVertices()
	 * vertices.
	 * @param Normal Points to the array that will receive normal vectors.
	 * The array behind the iterator must be as large as the Position array.
	 */
	static void CopyPositionAndNormals(
		const CModelDefPtr& mdef,
		const VertexArrayIterator<CVector3D>& Position,
		const VertexArrayIterator<CVector3D>& Normal);

	/**
	 * BuildPositionAndNormals: Build animated vertices and normals,
	 * transformed into world space.
	 *
	 * @param model The model that is to be transformed.
	 * @param Position Points to the array that will receive
	 * transformed position vectors. The array behind the iterator
	 * must be large enough to hold model->GetModelDef()->GetNumVertices()
	 * vertices. It must allow 16 bytes to be written to each element
	 * (i.e. provide 4 bytes of padding after each CVector3D).
	 * @param Normal Points to the array that will receive transformed
	 * normal vectors. The array behind the iterator must be as large as
	 * the Position array.
	 */
	static void BuildPositionAndNormals(
		CModel* model,
		const VertexArrayIterator<CVector3D>& Position,
		const VertexArrayIterator<CVector3D>& Normal);

	/**
	 * BuildUV: Copy UV coordinates into the given vertex array.
	 *
	 * @param mdef The model def.
	 * @param UV Points to the array that will receive UV coordinates.
	 * The array behind the iterator must large enough to hold
	 * mdef->GetNumVertices() vertices.
	 */
	static void BuildUV(
		const CModelDefPtr& mdef,
		const VertexArrayIterator<float[2]>& UV,
		int UVset);

	/**
	 * BuildIndices: Create the indices array for the given CModelDef.
	 *
	 * @param mdef The model definition object.
	 * @param Indices The index array, must be able to hold
	 * mdef->GetNumFaces()*3 elements.
	 */
	static void BuildIndices(
		const CModelDefPtr& mdef, const VertexArrayIterator<std::uint16_t>& Indices);

	/**
	 * GenTangents: Generate tangents for the given CModelDef.
	 *
	 * @param mdef The model definition object.
	 * @param newVertices An out vector of the unindexed vertices with tangents added.
	 * The new vertices cannot be used with existing face index and must be welded/reindexed.
	 */
	static void GenTangents(const CModelDefPtr& mdef, std::vector<float>& newVertices, bool gpuSkinning);

	/**
	 * Return the vertex attributes of the instance transform.
	 */
	static std::array<Renderer::Backend::SVertexAttributeFormat, 4> GetInstanceTransformAttributes(
		uint32_t stride = MODEL_INSTANCE_STRIDE);

	/**
	 * Bind a model's transform as instance attributes.
	 */
	static void BindModelTransform(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const CModel& model);

private:
	struct SSortCache;
	std::unique_ptr<SSortCache> m_SortCache;
};

#endif // INCLUDED_MODELRENDERER
