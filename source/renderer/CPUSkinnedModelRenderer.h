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

#ifndef INCLUDED_RENDERER_CPUSKINNEDMODELRENDERER
#define INCLUDED_RENDERER_CPUSKINNEDMODELRENDERER

#include "renderer/ModelVertexRenderer.h"

#include <memory>
#include <span>

class CModel;
class CModelRData;
namespace Renderer::Backend { class IDeviceCommandContext; }

/**
 * Render animated models using a ShaderRenderModifier.
 * It calculates vertex data for models on the CPU side.
 * This computes and binds per-vertex data; the modifier is responsible
 * for setting any shader uniforms etc.
 */
class CPUSkinnedModelVertexRenderer final : public ModelVertexRenderer
{
public:
	CPUSkinnedModelVertexRenderer();
	~CPUSkinnedModelVertexRenderer();

	CModelRData* CreateModelData(const void* key, CModel* model) override;
	void UpdateModelsData(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		std::span<CModel*> models) override;

	void UploadModelsData(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		std::span<CModel*> models) override;

	void PrepareModelDef(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		const CModelDef& def) override;
	void RenderModel(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		Renderer::Backend::IShaderProgram* shader, CModel* model, CModelRData* data) override;
	void RenderModels(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		Renderer::Backend::IShaderProgram* shader, const std::span<CModel* const> models) override;

	/**
	 * For CPU skinning we need to mark dynamic buffer chunks as needed before rendering.
	 * It must be called before uploading.
	 */
	void PrepareForRendering(std::span<CModel*> models);

protected:
	void UploadModelData(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		CModel* model, CModelRData* data);

	struct Internals;
	std::unique_ptr<Internals> m;
};


#endif // INCLUDED_RENDERER_CPUSKINNEDMODELRENDERER
