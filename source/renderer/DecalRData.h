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

#ifndef INCLUDED_DECALRDATA
#define INCLUDED_DECALRDATA

#include "graphics/Material.h"
#include "graphics/RenderableObject.h"
#include "lib/code_annotation.h"
#include "maths/Vector2D.h"
#include "maths/Vector3D.h"
#include "renderer/VertexBufferManager.h"
#include <vector>

class CModelDecal;
class CShaderDefines;
class IGroundView;
class ShadowMap;
namespace Renderer::Backend { class IDeviceCommandContext; }
namespace Renderer::Backend { class IVertexInputLayout; }

class CDecalRData : public CRenderData
{
public:
	CDecalRData(CModelDecal* decal, const IGroundView* ground);
	~CDecalRData();

	static Renderer::Backend::IVertexInputLayout* GetVertexInputLayout();

	void Update(const IGroundView* ground);

	static void RenderDecals(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		Renderer::Backend::IVertexInputLayout* vertexInputLayout,
		const std::vector<CDecalRData*>& decals, const CShaderDefines& context,
		ShadowMap* shadow, const CMaterial::Pass materialPass);

	CModelDecal* GetDecal() { return m_Decal; }

private:
	void BuildVertexData();

	struct SDecalVertex
	{
		CVector3D m_Position;
		CVector3D m_Normal;
		CVector2D m_UV;
	};
	cassert(sizeof(SDecalVertex) == 32);

	CVertexBufferManager::Handle m_VBDecals;
	CVertexBufferManager::Handle m_VBDecalsIndices;

	CModelDecal* m_Decal;

	const IGroundView* m_Ground;
};

#endif // INCLUDED_DECALRDATA
