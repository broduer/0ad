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

#ifndef INCLUDED_MINIMAPTEXTURE
#define INCLUDED_MINIMAPTEXTURE

#include "graphics/Color.h"
#include "graphics/ShaderTechniquePtr.h"
#include "graphics/Texture.h"
#include "lib/code_annotation.h"
#include "lib/posix/posix_types.h"
#include "maths/Vector2D.h"
#include "renderer/VertexArray.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CLOSTexture;
class CSimulation2;
class CTerrain;
class CTerritoryTexture;
namespace Renderer::Backend { class IDevice; }
namespace Renderer::Backend { class IDeviceCommandContext; }
namespace Renderer::Backend { class IFramebuffer; }
namespace Renderer::Backend { class ITexture; }
namespace Renderer::Backend { class IVertexInputLayout; }

struct MinimapUnitVertex
{
	std::uint8_t r, g, b, a;
	CVector2D position;
};

class CMiniMapTexture
{
	NONCOPYABLE(CMiniMapTexture);
public:
	CMiniMapTexture(Renderer::Backend::IDevice* device, CSimulation2& simulation);
	~CMiniMapTexture();

	void RequestRendering();

	struct Icon;
	struct Gathered
	{
		bool valid = false;
		std::vector<MinimapUnitVertex> entities;
		std::vector<MinimapUnitVertex> pings;
		std::vector<Icon> icons;
		bool losCircular = false;
	};
	// What a logic step hands to the render thread.
	struct Handover
	{
		// Whether the GUI asked for a render.
		bool render = false;
		Gathered gathered;
	};

	/**
	 * Gather entities and icons if due. Logic thread only.
	 */
	void Prepare(float realTimeDelta);

	/**
	 * Hand over what Prepare gathered, with the requests made since the last
	 * step. Logic thread only.
	 */
	Handover Publish();

	/**
	 * Take a published handover for Render. Render thread.
	 */
	void TakePublished(Handover&& handover);

	/**
	 * Redraws the texture if it's dirty.
	 */
	void Render(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		CLOSTexture& losTexture, CTerritoryTexture& territoryTexture);

	const CTexturePtr& GetTexture() const { return m_FinalTexture; }

	bool IsFlipped() const { return m_Flipped; }

	bool IsLosCircular() const { return m_LosCircular; }

	/**
	 * @return The maximum height for unit passage in water.
	 */
	static float GetShallowPassageHeight();

	struct Icon
	{
		CTexturePtr texture;
		CColor color;
		CVector2D worldPosition;
		float halfSize;
	};
	// Returns icons for corresponding entities on the minimap texture.
	const std::vector<Icon>& GetIcons() { return m_Icons; }

private:
	void CreateTextures(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		const CTerrain& terrain);
	void DestroyTextures();
	void RebuildTerrainTexture(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		const CTerrain& terrain);
	void RenderFinalTexture(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		CLOSTexture& losTexture, CTerritoryTexture& territoryTexture);
	/**
	 * Collect the entities and icons to draw from the simulation. Logic
	 * thread; the render thread uploads the result.
	 */
	void GatherEntities(const double currentTime);
	void UploadEntities(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		const std::vector<MinimapUnitVertex>& entities,
		const std::vector<MinimapUnitVertex>& pings, const float entityRadius);
	void DrawEntities(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		const float entityRadius);

	CSimulation2& m_Simulation;

	// Logic thread; Publish hands this to the render thread.
	bool m_RenderRequested = false;
	// Whether to gather entities before the update interval has passed.
	bool m_FinalDirty = true;

	// Render thread.
	bool m_RenderingRequested = false;
	bool m_TerrainTextureDirty = true;
	bool m_Flipped = false;

	// minimap texture handles
	std::unique_ptr<Renderer::Backend::ITexture> m_TerrainTexture;

	CTexturePtr m_FinalTexture;

	std::unique_ptr<Renderer::Backend::IFramebuffer>
		m_FinalTextureFramebuffer;

	// texture data
	std::unique_ptr<std::uint32_t[]> m_TerrainData;

	// map size
	ssize_t m_MapSize = 0;

	// Maximal water height to allow the passage of a unit (for underwater shallows).
	float m_ShallowPassageHeight = 0.0f;
	float m_WaterHeight = 0.0f;

	Renderer::Backend::IVertexInputLayout* m_QuadVertexInputLayout = nullptr;
	Renderer::Backend::IVertexInputLayout* m_EntitiesVertexInputLayout = nullptr;

	VertexIndexArray m_IndexArray;
	VertexArray m_VertexArray;
	VertexArray::Attribute m_AttributePos;
	VertexArray::Attribute m_AttributeColor;

	bool m_UseInstancing = false;
	// Vertex data if instancing is supported.
	VertexArray m_InstanceVertexArray;
	VertexArray::Attribute m_InstanceAttributePosition;

	CShaderTechniquePtr m_TerritoryTechnique;

	size_t m_EntitiesDrawn = 0;

	double m_PingDuration = 25.0;
	double m_HalfBlinkDuration = 0.0;
	double m_NextBlinkTime = 0.0;
	bool m_BlinkState = false;

	std::vector<Icon> m_Icons;
	// Logic thread: real time summed over frame steps, which paces gathering,
	// and when entities were last gathered.
	double m_LogicRealTime = 0.0;
	double m_LastEntityGather = 0.0;
	// Gathered on the logic thread and handed over in the frame packet, then
	// the render thread's until drawn.
	Gathered m_Prepared;
	Gathered m_Taken;
	std::atomic<bool> m_LosCircular{false};
	// We store the map as a member to avoid redundant reallocations on each
	// update. We use a grid approach to combine icons by distance.
	struct CellIconKey
	{
		std::string path;
		std::uint8_t r, g, b;
	};
	struct CellIconKeyHash
	{
		size_t operator()(const CellIconKey& key) const;
	};
	struct CellIconKeyEqual
	{
		bool operator()(const CellIconKey& lhs, const CellIconKey& rhs) const;
	};
	struct CellIcon
	{
		// TODO: use CVector2DI.
		std::uint16_t gridX, gridY;
		float halfSize;
		CVector2D worldPosition;
	};
	std::unordered_map<CellIconKey, std::vector<CellIcon>, CellIconKeyHash, CellIconKeyEqual> m_IconsCache;
};

#endif // INCLUDED_MINIMAPTEXTURE
