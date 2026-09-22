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

#include "ICmpTerritoryManager.h"

#include "graphics/Color.h"
#include "graphics/FramePacket.h"
#include "graphics/Overlay.h"
#include "graphics/SColor.h"
#include "graphics/TerritoryBoundary.h"
#include "graphics/Texture.h"
#include "graphics/TextureManager.h"
#include "lib/code_annotation.h"
#include "lib/code_generation.h"
#include "lib/debug.h"
#include "lib/path.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "maths/MathUtil.h"
#include "maths/Vector2D.h"
#include "ps/Filesystem.h"
#include "ps/Profile.h"
#include "ps/XML/Xeromyces.h"
#include "renderer/Renderer.h"
#include "renderer/Scene.h"
#include "renderer/TerrainOverlay.h"
#include "renderer/backend/Sampler.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpFootprint.h"
#include "simulation2/components/ICmpObstructionManager.h"
#include "simulation2/components/ICmpOwnership.h"
#include "simulation2/components/ICmpPathfinder.h"
#include "simulation2/components/ICmpPlayer.h"
#include "simulation2/components/ICmpPlayerManager.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpTerritoryDecayManager.h"
#include "simulation2/components/ICmpTerritoryInfluence.h"
#include "simulation2/helpers/Grid.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/helpers/Render.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Message.h"
#include "simulation2/system/SimContext.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class CCmpTerritoryManager;

namespace
{
constexpr bool DISABLE_TERRITORY_OVERLAY{true};

void BuildTextureRGBA(Grid<std::uint8_t>*& territories, std::uint8_t* data, const int w, const int h)
{
	TerrainTextureOverlay::OverwriteEachTile(data, w, h, [&](const int i, const int j)
	{
		const std::uint8_t id{static_cast<std::uint8_t>(territories->get(j, i) &
			ICmpTerritoryManager::TERRITORY_PLAYER_MASK)};
		return TerrainTextureOverlay::GetColor(id, 64);
	});
}
}

class CCmpTerritoryManager : public ICmpTerritoryManager
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeGloballyToMessageType(MT_OwnershipChanged);
		componentManager.SubscribeGloballyToMessageType(MT_PlayerColorChanged);
		componentManager.SubscribeGloballyToMessageType(MT_PositionChanged);
		componentManager.SubscribeGloballyToMessageType(MT_ValueModification);
		componentManager.SubscribeToMessageType(MT_ObstructionMapShapeChanged);
		componentManager.SubscribeToMessageType(MT_TerrainChanged);
		componentManager.SubscribeToMessageType(MT_WaterChanged);
		componentManager.SubscribeToMessageType(MT_Update);
		componentManager.SubscribeToMessageType(MT_FramePrepare);
		componentManager.SubscribeToMessageType(MT_FrameStep);
	}

	DEFAULT_COMPONENT_ALLOCATOR(TerritoryManager)

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	std::uint8_t m_ImpassableCost;
	float m_BorderThickness;
	float m_BorderSeparation;

	// Player ID   in bits 0-4 (TERRITORY_PLAYER_MASK)
	// connected flag in bit 5 (TERRITORY_CONNECTED_MASK)
	// blinking flag  in bit 6 (TERRITORY_BLINKING_MASK)
	// processed flag in bit 7 (TERRITORY_PROCESSED_MASK)
	Grid<std::uint8_t>* m_Territories;

	std::vector<std::uint16_t> m_TerritoryCellCounts;
	std::uint16_t m_TerritoryTotalPassableCellCount;

	// Saves the cost per tile (to stop territory on impassable tiles)
	Grid<std::uint8_t>* m_CostGrid;

	// Set to true when territories change; will send a TerritoriesChanged message
	// during the Update phase
	bool m_TriggerEvent;

	// Prepared on the logic thread and recorded for the render world at the next frame step.
	std::vector<RenderWorld::STerritoryLine> m_PreparedBoundaryLines;
	std::vector<SOverlayLine> m_PreparedDebugBoundaryLineNodes;
	bool m_LinesPrepared = false;
	std::map<player_id_t, CColor> m_PreparedColors;
	bool m_ColorsPrepared = false;
	bool m_BoundaryLinesDirty;
	bool m_DebugOverlayEnabled;

	bool m_EnableLineDebugOverlays; ///< Enable node debugging overlays for boundary lines?

	void Init(const CParamNode&) override
	{
		m_Territories = NULL;
		m_CostGrid = NULL;
		m_DebugOverlayEnabled = !DISABLE_TERRITORY_OVERLAY;
		m_BoundaryLinesDirty = true;
		m_TriggerEvent = true;
		m_EnableLineDebugOverlays = false;
		m_DirtyID = 1;
		m_DirtyBlinkingID = 1;
		m_ColorChanged = false;

		m_TerritoryTotalPassableCellCount = 0;

		// Register Relax NG validator
		g_Xeromyces.AddValidator(g_VFS, "territorymanager", "simulation/data/territorymanager.rng");

		CParamNode externalParamNode;
		CParamNode::LoadXML(externalParamNode, L"simulation/data/territorymanager.xml", "territorymanager");

		int impassableCost = externalParamNode.GetChild("TerritoryManager").GetChild("ImpassableCost").ToInt();
		ENSURE(0 <= impassableCost && impassableCost <= 255);
		m_ImpassableCost = static_cast<std::uint8_t>(impassableCost);

		const std::string& visibilityStatus = externalParamNode.GetChild("TerritoryManager").GetChild("VisibilityStatus").ToString();
		m_Enabled = visibilityStatus != "off";
		m_Visible = m_Enabled && visibilityStatus == "visible";

		m_BorderThickness = externalParamNode.GetChild("TerritoryManager").GetChild("BorderThickness").ToFixed().ToFloat();
		m_BorderSeparation = externalParamNode.GetChild("TerritoryManager").GetChild("BorderSeparation").ToFixed().ToFloat();
	}

	void Deinit() override
	{
		SAFE_DELETE(m_Territories);
		SAFE_DELETE(m_CostGrid);
	}

	void Serialize(ISerializer& serialize) override
	{
		// Territory state can be recomputed as required, so we don't need to serialize any of it.
		serialize.Bool("trigger event", m_TriggerEvent);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);
		deserialize.Bool("trigger event", m_TriggerEvent);
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_OwnershipChanged:
		{
			const CMessageOwnershipChanged& msgData = static_cast<const CMessageOwnershipChanged&> (msg);
			MakeDirtyIfRelevantEntity(msgData.entity);
			break;
		}
		case MT_PlayerColorChanged:
		{
			MakeDirty();
			break;
		}
		case MT_PositionChanged:
		{
			const CMessagePositionChanged& msgData = static_cast<const CMessagePositionChanged&> (msg);
			MakeDirtyIfRelevantEntity(msgData.entity);
			break;
		}
		case MT_ValueModification:
		{
			const CMessageValueModification& msgData = static_cast<const CMessageValueModification&> (msg);
			if (msgData.component == L"TerritoryInfluence")
				MakeDirty();
			break;
		}
		case MT_ObstructionMapShapeChanged:
		case MT_TerrainChanged:
		case MT_WaterChanged:
		{
			// also recalculate the cost grid to support atlas changes
			SAFE_DELETE(m_CostGrid);
			MakeDirty();
			break;
		}
		case MT_Update:
		{
			if (m_TriggerEvent)
			{
				m_TriggerEvent = false;
				GetSimContext().GetComponentManager().BroadcastMessage(CMessageTerritoriesChanged());
			}
			break;
		}
		case MT_FramePrepare:
		{
			if (m_BoundaryLinesDirty)
			{
				UpdateBoundaryLines();
				m_BoundaryLinesDirty = false;
			}
			break;
		}
		case MT_FrameStep:
		{
			PublishRenderState();
			break;
		}
		}
	}

	// Check whether the entity is either a settlement or territory influence;
	// ignore any others
	void MakeDirtyIfRelevantEntity(entity_id_t ent)
	{
		CmpPtr<ICmpTerritoryInfluence> cmpTerritoryInfluence(GetSimContext(), ent);
		if (cmpTerritoryInfluence)
			MakeDirty();
	}

	const Grid<std::uint8_t>& GetTerritoryGrid() override
	{
		CalculateTerritories();
		ENSURE(m_Territories);
		return *m_Territories;
	}

	player_id_t GetOwner(entity_pos_t x, entity_pos_t z) override;
	std::vector<std::uint32_t> GetNeighbours(entity_pos_t x, entity_pos_t z, bool filterConnected) override;
	bool IsConnected(entity_pos_t x, entity_pos_t z) override;

	void SetTerritoryBlinking(entity_pos_t x, entity_pos_t z, bool enable) override;
	bool IsTerritoryBlinking(entity_pos_t x, entity_pos_t z) override;

	// To support lazy updates of territory render data,
	// we maintain a DirtyID here and increment it whenever territories change;
	// if a caller has a lower DirtyID then it needs to be updated.
	// We also do the same thing for blinking updates using DirtyBlinkingID.

	size_t m_DirtyID;
	size_t m_DirtyBlinkingID;

	bool m_ColorChanged;

	void MakeDirty()
	{
		SAFE_DELETE(m_Territories);
		++m_DirtyID;
		m_BoundaryLinesDirty = true;
		m_TriggerEvent = true;
	}

	bool NeedUpdateTexture(size_t* dirtyID) override
	{
		if (*dirtyID == m_DirtyID && !m_ColorChanged)
			return false;

		*dirtyID = m_DirtyID;
		m_ColorChanged = false;
		return true;
	}

	bool NeedUpdateAI(size_t* dirtyID, size_t* dirtyBlinkingID) const override
	{
		if (*dirtyID == m_DirtyID && *dirtyBlinkingID == m_DirtyBlinkingID)
			return false;

		*dirtyID = m_DirtyID;
		*dirtyBlinkingID = m_DirtyBlinkingID;
		return true;
	}

	void CalculateCostGrid();

	void CalculateTerritories();

	std::uint8_t GetTerritoryPercentage(player_id_t player) override;

	std::vector<STerritoryBoundary> ComputeBoundaries();

	void UpdateBoundaryLines();

	void SetVisibility(bool visible) override
	{
		if (!m_Enabled)
			return;

		m_Visible = visible;
	}

	/**
	 * Record the prepared boundary lines, colours and visibility for the render world.
	 */
	void PublishRenderState();

	bool IsVisible() const override
	{
		return m_Enabled && m_Visible;
	}

	void UpdateColors() override;

private:
	bool m_Visible;
	bool m_Enabled;

	/**
	* Mark the territory tiles covered by an entity's footprint with infinite weight.
	* This ensures that buildings always own the territory directly beneath them.
	* Tiles already claimed by another footprint are left untouched, so when called
	* in ascending entity ID order, the oldest building keeps contested tiles.
	*/
	void MarkFootprintTiles(
		entity_id_t ent,
		player_id_t owner,
		Grid<std::uint32_t>& bestWeightGrid,
		const std::uint16_t tilesW,
		const std::uint16_t tilesH,
		std::vector<std::pair<std::uint16_t, std::uint16_t>>& tiles);
};

REGISTER_COMPONENT_TYPE(TerritoryManager)

// Tile data type, for easier accessing of coordinates
struct Tile
{
	Tile(std::uint16_t i, std::uint16_t j) : x(i), z(j) { }
	std::uint16_t x, z;
};

/**
 * Queue based eight directional floodfill algorithm.
 *
 * @param origin Where to start the floodfill. In the first iteration it is
 *	passed as the second argument to the @see decider and the floodfill only
 *	continues when the invocation returns @c true.
 * @param gridSize Tiles outside the boundary are never exteded. The
 *	@see decider isn't called with thous tiles.
 * @param decider It is called with a tile wich was already added as the first
 *	argument and a neighbour as the second argument. The invocation shall
 *	return whether to extend the wavefront to the neighbour (if allways
 *	@c true is returned, an infinite loop will occur). In the first iteration
 *	the @see decider is invoked with a null pointer as the first argument and
 *	@see origin as the second argument.
 */
template<typename Decider>
void Floodfill(const Tile& origin, const Tile& gridSize, Decider decider)
{
	static_assert(std::is_invocable_r_v<bool, Decider, const Tile*, const Tile&>);

	constexpr std::array<std::array<int, 2>, 8> neighbours{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1},
		{-1, -1}, {1, -1}, {-1, 1}}};
	std::queue<Tile> openTiles;

	const auto emplaceIfRequested = [decider = std::move(decider), &openTiles](
		const Tile* currentTile, const Tile& neighbourTile)
		{
			if (decider(currentTile, neighbourTile))
				openTiles.emplace(neighbourTile);
		};

	emplaceIfRequested(nullptr, origin);
	while (!openTiles.empty())
	{
		const Tile currentTile{openTiles.front()};
		openTiles.pop();
		for (const std::array<int, 2>& neighbour : neighbours)
		{
			const Tile neighbourTile{
				static_cast<std::uint16_t>(currentTile.x + std::get<0>(neighbour)),
				static_cast<std::uint16_t>(currentTile.z + std::get<1>(neighbour))};

			// Check the bounds, underflow will cause the values to be big again.
			if (neighbourTile.x < gridSize.x && neighbourTile.z < gridSize.z)
				emplaceIfRequested(&currentTile, neighbourTile);
		}
	}
}

/**
 * Compute the tile indexes on the grid nearest to a given point
 */
static void NearestTerritoryTile(entity_pos_t x, entity_pos_t z, std::uint16_t& i, std::uint16_t& j,
	std::uint16_t w, std::uint16_t h)
{
	entity_pos_t scale = Pathfinding::NAVCELL_SIZE * ICmpTerritoryManager::NAVCELLS_PER_TERRITORY_TILE;
	i = Clamp((x / scale).ToInt_RoundToNegInfinity(), 0, w - 1);
	j = Clamp((z / scale).ToInt_RoundToNegInfinity(), 0, h - 1);
}

void CCmpTerritoryManager::CalculateCostGrid()
{
	if (m_CostGrid)
		return;

	CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
	if (!cmpPathfinder)
		return;

	pass_class_t passClassTerritory = cmpPathfinder->GetPassabilityClass("default-terrain-only");
	pass_class_t passClassUnrestricted = cmpPathfinder->GetPassabilityClass("unrestricted");

	const Grid<NavcellData>& passGrid = cmpPathfinder->GetPassabilityGrid();

	int tilesW = passGrid.m_W / NAVCELLS_PER_TERRITORY_TILE;
	int tilesH = passGrid.m_H / NAVCELLS_PER_TERRITORY_TILE;

	m_CostGrid = new Grid<std::uint8_t>(tilesW, tilesH);
	m_TerritoryTotalPassableCellCount = 0;

	for (int i = 0; i < tilesW; ++i)
	{
		for (int j = 0; j < tilesH; ++j)
		{
			NavcellData c = 0;
			for (std::uint16_t di = 0; di < NAVCELLS_PER_TERRITORY_TILE; ++di)
				for (std::uint16_t dj = 0; dj < NAVCELLS_PER_TERRITORY_TILE; ++dj)
					c |= passGrid.get(
						i * NAVCELLS_PER_TERRITORY_TILE + di,
						j * NAVCELLS_PER_TERRITORY_TILE + dj);
			if (!IS_PASSABLE(c, passClassTerritory))
				m_CostGrid->set(i, j, m_ImpassableCost);
			else if (!IS_PASSABLE(c, passClassUnrestricted))
				m_CostGrid->set(i, j, 255); // off the world; use maximum cost
			else
			{
				m_CostGrid->set(i, j, 1);
				++m_TerritoryTotalPassableCellCount;
			}
		}
	}
}

void CCmpTerritoryManager::MarkFootprintTiles(
	entity_id_t ent,
	player_id_t owner,
	Grid<std::uint32_t>& bestWeightGrid,
	const std::uint16_t tilesW,
	const std::uint16_t tilesH,
	std::vector<std::pair<std::uint16_t, std::uint16_t>>& tiles)
{
	// Get the footprint component
	CmpPtr<ICmpFootprint> cmpFootprint{GetSimContext(), ent};
	if (!cmpFootprint)
		return;

	// Calculate tile size
	const entity_pos_t tileSize{Pathfinding::NAVCELL_SIZE * NAVCELLS_PER_TERRITORY_TILE};

	// Get the tiles covered by this footprint
	cmpFootprint->GetGridTiles(tileSize, tilesW, tilesH, tiles);

	const std::uint32_t infiniteWeight = std::numeric_limits<std::uint32_t>::max();

	// Apply infinite weight to all footprint tiles not yet claimed by another footprint
	for (const auto& tile : tiles)
	{
		if (bestWeightGrid.get(tile.first, tile.second) == infiniteWeight)
			continue;

		m_Territories->set(tile.first, tile.second, owner);
		bestWeightGrid.set(tile.first, tile.second, infiniteWeight);
	}
}

void CCmpTerritoryManager::CalculateTerritories()
{
	if (m_Territories)
		return;

	PROFILE("CalculateTerritories");

	// If the pathfinder hasn't been loaded (e.g. this is called during map initialisation),
	// abort the computation (and assume callers can cope with m_Territories == NULL)
	CalculateCostGrid();
	if (!m_CostGrid)
		return;

	const std::uint16_t tilesW = m_CostGrid->m_W;
	const std::uint16_t tilesH = m_CostGrid->m_H;

	m_Territories = new Grid<std::uint8_t>(tilesW, tilesH);

	// Reset territory counts for all players
	CmpPtr<ICmpPlayerManager> cmpPlayerManager(GetSystemEntity());
	if (cmpPlayerManager && (size_t)cmpPlayerManager->GetNumPlayers() != m_TerritoryCellCounts.size())
		m_TerritoryCellCounts.resize(cmpPlayerManager->GetNumPlayers());
	for (std::uint16_t& count : m_TerritoryCellCounts)
		count = 0;

	// Find all territory influence entities (sorted by entity ID)
	CComponentManager::InterfaceList influences = GetSimContext().GetComponentManager().GetEntitiesWithInterface(IID_TerritoryInfluence);

	// Store the overall best weight for comparison
	Grid<std::uint32_t> bestWeightGrid(tilesW, tilesH);

	// Reusable buffer for footprint tiles - avoids per-entity allocations
	std::vector<std::pair<std::uint16_t, std::uint16_t>> footprintTiles;

	// Split influence entities into per-player lists, ignoring any with invalid properties,
	// and mark their footprint tiles in entity ID order so the oldest building wins overlaps.
	std::map<player_id_t, std::vector<entity_id_t> > influenceEntities;
	for (const CComponentManager::InterfacePair& pair : influences)
	{
		entity_id_t ent = pair.first;

		CmpPtr<ICmpOwnership> cmpOwnership(GetSimContext(), ent);
		if (!cmpOwnership)
			continue;

		// Ignore Gaia and unassigned or players we can't represent
		player_id_t owner = cmpOwnership->GetOwner();
		if (owner <= 0 || owner > TERRITORY_PLAYER_MASK)
			continue;

		// Ignore entities that project no territory, they shouldn't claim their footprint either
		const ICmpTerritoryInfluence* const cmpTerritoryInfluence{static_cast<const ICmpTerritoryInfluence*>(pair.second)};
		if (cmpTerritoryInfluence->GetWeight() == 0 || cmpTerritoryInfluence->GetRadius() == 0)
			continue;

		influenceEntities[owner].push_back(ent);

		// Mark the entity's footprint tiles with infinite weight
		MarkFootprintTiles(ent, owner, bestWeightGrid, tilesW, tilesH, footprintTiles);
	}

	// store the root influences to mark territory as connected
	std::vector<entity_id_t> rootInfluenceEntities;

	for (const std::pair<const player_id_t, std::vector<entity_id_t>>& pair : influenceEntities)
	{
		// entityGrid stores the weight for a single entity, and is reset per entity
		Grid<std::uint32_t> entityGrid(tilesW, tilesH);
		// playerGrid stores the combined weight of all entities for this player
		Grid<std::uint32_t> playerGrid(tilesW, tilesH);

		std::uint8_t owner = static_cast<std::uint8_t>(pair.first);
		const std::vector<entity_id_t>& ents = pair.second;
		// With 2^16 entities, we're safe against overflows as the weight is also limited to 2^16
		ENSURE(ents.size() < 1 << 16);
		// Compute the influence map of the current entity, then add it to the player grid
		for (entity_id_t ent : ents)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), ent);
			if (!cmpPosition || !cmpPosition->IsInWorld())
				continue;

			CmpPtr<ICmpTerritoryInfluence> cmpTerritoryInfluence(GetSimContext(), ent);
			const std::uint32_t originWeight = cmpTerritoryInfluence->GetWeight();
			// Non-zero, entities with zero weight or radius were filtered out above
			const std::uint32_t radius = cmpTerritoryInfluence->GetRadius();
			const std::uint32_t relativeFalloff = originWeight *
				(Pathfinding::NAVCELL_SIZE * NAVCELLS_PER_TERRITORY_TILE)
				.ToInt_RoundToNegInfinity() / radius;

			CFixedVector2D pos = cmpPosition->GetPosition2D();
			std::uint16_t i, j;
			NearestTerritoryTile(pos.X, pos.Y, i, j, tilesW, tilesH);

			if (cmpTerritoryInfluence->IsRoot())
				rootInfluenceEntities.push_back(ent);

			// Expand influences outwards
			Floodfill({i, j}, {tilesW, tilesH}, [&](const Tile* current, const Tile& neighbour)
				{
					const bool diagonalProgression{current && neighbour.x != current->x &&
						neighbour.z != current->z};

					const std::uint32_t falloffPerTile{relativeFalloff *
						m_CostGrid->get(neighbour.x, neighbour.z)};
					// diagonal neighbour -> multiply with approx sqrt(2)
					const std::uint32_t falloff{diagonalProgression ? (falloffPerTile * 362) / 256 :
						falloffPerTile};

					// Don't expand if new cost is not better than previous value for that tile
					// (arranged to avoid underflow if entityGrid.get(x, z) < falloff)
					if (current &&
						entityGrid.get(current->x, current->z) <=
							entityGrid.get(neighbour.x, neighbour.z) + falloff)
						{
							return false;
						}

					// weight of this tile = weight of predecessor - falloff from predecessor
					const std::uint32_t weight{current ?
						entityGrid.get(current->x, current->z) - falloff : originWeight};
					const std::uint32_t totalWeight{weight + (current ?
						playerGrid.get(neighbour.x, neighbour.z) -
						entityGrid.get(neighbour.x, neighbour.z) : 0)};

					playerGrid.set(neighbour.x, neighbour.z, totalWeight);
					entityGrid.set(neighbour.x, neighbour.z, weight);
					// if this weight is better than the best thus far, set the owner
					if (totalWeight > bestWeightGrid.get(neighbour.x, neighbour.z))
					{
						bestWeightGrid.set(neighbour.x, neighbour.z, totalWeight);
						m_Territories->set(neighbour.x, neighbour.z, owner);
					}
					return true;
				});
			entityGrid.reset();
		}
	}

	// Detect territories connected to a 'root' influence (typically a civ center)
	// belonging to their player, and mark them with the connected flag
	for (entity_id_t ent : rootInfluenceEntities)
	{
		// (These components must be valid else the entities wouldn't be added to this list)
		CmpPtr<ICmpOwnership> cmpOwnership(GetSimContext(), ent);
		CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), ent);

		CFixedVector2D pos = cmpPosition->GetPosition2D();
		std::uint16_t i, j;
		NearestTerritoryTile(pos.X, pos.Y, i, j, tilesW, tilesH);

		std::uint8_t owner = static_cast<std::uint8_t>(cmpOwnership->GetOwner());

		Floodfill({i, j}, {tilesW, tilesH}, [&](const Tile*, const Tile& neighbour)
			{
				// Don't expand non-owner tiles, or tiles that already have a connected mask
				if (m_Territories->get(neighbour.x, neighbour.z) != owner)
					return false;
				m_Territories->set(neighbour.x, neighbour.z, owner | TERRITORY_CONNECTED_MASK);
				if (m_CostGrid->get(neighbour.x, neighbour.z) < m_ImpassableCost)
					++m_TerritoryCellCounts[owner];
				return true;
			});
	}

	// Then recomputes the blinking tiles
       	CmpPtr<ICmpTerritoryDecayManager> cmpTerritoryDecayManager(GetSystemEntity());
       	if (cmpTerritoryDecayManager)
	{
		size_t dirtyBlinkingID = m_DirtyBlinkingID;
	       	cmpTerritoryDecayManager->SetBlinkingEntities();
		m_DirtyBlinkingID = dirtyBlinkingID;
	}
}

std::vector<STerritoryBoundary> CCmpTerritoryManager::ComputeBoundaries()
{
	PROFILE("ComputeBoundaries");

	CalculateTerritories();
	ENSURE(m_Territories);

	return CTerritoryBoundaryCalculator::ComputeBoundaries(m_Territories);
}

std::uint8_t CCmpTerritoryManager::GetTerritoryPercentage(player_id_t player)
{
	if (player <= 0 || (m_Territories && static_cast<size_t>(player) >= m_TerritoryCellCounts.size()))
		return 0;

	CalculateTerritories();

	// Territories may have been recalculated, check whether player is still there.
	if (m_TerritoryTotalPassableCellCount == 0 || static_cast<size_t>(player) >= m_TerritoryCellCounts.size())
		return 0;

	std::uint8_t percentage = (m_TerritoryCellCounts[player] * 100) / m_TerritoryTotalPassableCellCount;
	ENSURE(percentage <= 100);
	return percentage;
}

void CCmpTerritoryManager::UpdateBoundaryLines()
{
	PROFILE("update boundary lines");

	m_PreparedBoundaryLines.clear();
	m_PreparedDebugBoundaryLineNodes.clear();

	if (!CRenderer::IsInitialised())
		return;

	std::vector<STerritoryBoundary> boundaries = ComputeBoundaries();

	CTextureProperties texturePropsBase("art/textures/misc/territory_border.png");
	texturePropsBase.SetAddressMode(
		Renderer::Backend::Sampler::AddressMode::CLAMP_TO_BORDER,
		Renderer::Backend::Sampler::AddressMode::CLAMP_TO_EDGE);
	texturePropsBase.SetAnisotropicFilter(true);
	CTexturePtr textureBase = g_Renderer.GetTextureManager().CreateTexture(texturePropsBase);

	CTextureProperties texturePropsMask("art/textures/misc/territory_border_mask.png");
	texturePropsMask.SetAddressMode(
		Renderer::Backend::Sampler::AddressMode::CLAMP_TO_BORDER,
		Renderer::Backend::Sampler::AddressMode::CLAMP_TO_EDGE);
	texturePropsMask.SetAnisotropicFilter(true);
	CTexturePtr textureMask = g_Renderer.GetTextureManager().CreateTexture(texturePropsMask);

	CmpPtr<ICmpPlayerManager> cmpPlayerManager(GetSystemEntity());
	if (!cmpPlayerManager)
		return;

	for (size_t i = 0; i < boundaries.size(); ++i)
	{
		if (boundaries[i].points.empty())
			continue;

		CColor color(1, 0, 1, 1);
		CmpPtr<ICmpPlayer> cmpPlayer(GetSimContext(), cmpPlayerManager->GetPlayerByID(boundaries[i].owner));
		if (cmpPlayer)
			color = cmpPlayer->GetDisplayedColor();

		m_PreparedBoundaryLines.push_back(RenderWorld::STerritoryLine());
		m_PreparedBoundaryLines.back().blinking = boundaries[i].blinking;
		m_PreparedBoundaryLines.back().owner = boundaries[i].owner;
		m_PreparedBoundaryLines.back().color = color;
		m_PreparedBoundaryLines.back().overlay.m_TextureBase = textureBase;
		m_PreparedBoundaryLines.back().overlay.m_TextureMask = textureMask;
		m_PreparedBoundaryLines.back().overlay.m_Color = color;
		m_PreparedBoundaryLines.back().overlay.m_Thickness = m_BorderThickness;
		m_PreparedBoundaryLines.back().overlay.m_Closed = true;

		SimRender::SmoothPointsAverage(boundaries[i].points, m_PreparedBoundaryLines.back().overlay.m_Closed);
		SimRender::InterpolatePointsRNS(boundaries[i].points, m_PreparedBoundaryLines.back().overlay.m_Closed, m_BorderSeparation);

		std::vector<CVector2D>& points = m_PreparedBoundaryLines.back().overlay.m_Coords;
		for (size_t j = 0; j < boundaries[i].points.size(); ++j)
		{
			points.push_back(boundaries[i].points[j]);

			if (m_EnableLineDebugOverlays)
			{
				const size_t numHighlightNodes = 7; // highlight the X last nodes on either end to see where they meet (if closed)
				SOverlayLine overlayNode;
				if (j > boundaries[i].points.size() - 1 - numHighlightNodes)
					overlayNode.m_Color = CColor(1.f, 0.f, 0.f, 1.f);
				else if (j < numHighlightNodes)
					overlayNode.m_Color = CColor(0.f, 1.f, 0.f, 1.f);
				else
					overlayNode.m_Color = CColor(1.0f, 1.0f, 1.0f, 1.0f);

				overlayNode.m_Thickness = 0.1f;
				SimRender::ConstructCircleOnGround(GetSimContext(), boundaries[i].points[j].X, boundaries[i].points[j].Y, 0.1f, overlayNode, true);
				m_PreparedDebugBoundaryLineNodes.push_back(overlayNode);
			}
		}

	}
	m_LinesPrepared = true;
}

player_id_t CCmpTerritoryManager::GetOwner(entity_pos_t x, entity_pos_t z)
{
	std::uint16_t i, j;
	if (!m_Territories)
	{
		CalculateTerritories();
		if (!m_Territories)
			return 0;
	}

	NearestTerritoryTile(x, z, i, j, m_Territories->m_W, m_Territories->m_H);
	return m_Territories->get(i, j) & TERRITORY_PLAYER_MASK;
}

std::vector<std::uint32_t> CCmpTerritoryManager::GetNeighbours(entity_pos_t x, entity_pos_t z, bool filterConnected)
{
	CmpPtr<ICmpPlayerManager> cmpPlayerManager(GetSystemEntity());
	if (!cmpPlayerManager)
		return std::vector<std::uint32_t>();

	std::vector<std::uint32_t> ret(cmpPlayerManager->GetNumPlayers(), 0);
	CalculateTerritories();
	if (!m_Territories)
		return ret;

	std::uint16_t i, j;
	NearestTerritoryTile(x, z, i, j, m_Territories->m_W, m_Territories->m_H);

	// calculate the neighbours
	player_id_t thisOwner = m_Territories->get(i, j) & TERRITORY_PLAYER_MASK;

	std::uint16_t tilesW = m_Territories->m_W;
	std::uint16_t tilesH = m_Territories->m_H;

	// use a flood-fill algorithm that fills up to the borders and remembers the owners
	Grid<bool> markerGrid(tilesW, tilesH);

	Floodfill({i, j}, {tilesW, tilesH}, [&](const Tile*, const Tile& neighbour)
		{
			if (markerGrid.get(neighbour.x, neighbour.z))
				return false;
			// mark the tile as visited in any case
			markerGrid.set(neighbour.x, neighbour.z, true);
			int owner = m_Territories->get(neighbour.x, neighbour.z) & TERRITORY_PLAYER_MASK;
			if (owner != thisOwner)
			{
				if (owner == 0 || !filterConnected || (m_Territories->get(neighbour.x, neighbour.z) & TERRITORY_CONNECTED_MASK) != 0)
					ret[owner]++; // add player to the neighbour list when requested
				return false; // don't expand non-owner tiles further
			}
			return true;
		});

	return ret;
}

bool CCmpTerritoryManager::IsConnected(entity_pos_t x, entity_pos_t z)
{
	std::uint16_t i, j;
	CalculateTerritories();
	if (!m_Territories)
		return false;

	NearestTerritoryTile(x, z, i, j, m_Territories->m_W, m_Territories->m_H);
	return (m_Territories->get(i, j) & TERRITORY_CONNECTED_MASK) != 0;
}

void CCmpTerritoryManager::SetTerritoryBlinking(entity_pos_t x, entity_pos_t z, bool enable)
{
	CalculateTerritories();
	if (!m_Territories)
		return;

	std::uint16_t i, j;
	NearestTerritoryTile(x, z, i, j, m_Territories->m_W, m_Territories->m_H);

	std::uint16_t tilesW = m_Territories->m_W;
	std::uint16_t tilesH = m_Territories->m_H;

	player_id_t thisOwner = m_Territories->get(i, j) & TERRITORY_PLAYER_MASK;

	Floodfill({i, j}, {tilesW, tilesH}, [&](const Tile*, const Tile& neighbour)
		{
			const std::uint8_t bitmask{m_Territories->get(neighbour.x, neighbour.z)};
			if ((bitmask & TERRITORY_PLAYER_MASK) != thisOwner)
				return false;
			const bool blinking{(bitmask & TERRITORY_BLINKING_MASK) != 0};
			if (enable != blinking)
			{
				m_Territories->set(neighbour.x, neighbour.z, enable ?
					bitmask | TERRITORY_BLINKING_MASK : bitmask & ~TERRITORY_BLINKING_MASK);
				return true;
			}
			return false;
		});
	++m_DirtyBlinkingID;
	m_BoundaryLinesDirty = true;
}

bool CCmpTerritoryManager::IsTerritoryBlinking(entity_pos_t x, entity_pos_t z)
{
	CalculateTerritories();
	if (!m_Territories)
		return false;

	std::uint16_t i, j;
	NearestTerritoryTile(x, z, i, j, m_Territories->m_W, m_Territories->m_H);
	return (m_Territories->get(i, j) & TERRITORY_BLINKING_MASK) != 0;
}

void CCmpTerritoryManager::UpdateColors()
{
	m_ColorChanged = true;

	CmpPtr<ICmpPlayerManager> cmpPlayerManager(GetSystemEntity());
	if (!cmpPlayerManager)
		return;

	// Look the colours up here, as this may run script and the frame step must not.
	m_PreparedColors.clear();
	const std::int32_t numPlayers = cmpPlayerManager->GetNumPlayers();
	for (std::int32_t player = 0; player < numPlayers; ++player)
	{
		CmpPtr<ICmpPlayer> cmpPlayer(GetSimContext(), cmpPlayerManager->GetPlayerByID(player));
		if (cmpPlayer)
			m_PreparedColors[player] = cmpPlayer->GetDisplayedColor();
	}
	m_ColorsPrepared = true;
}

void CCmpTerritoryManager::PublishRenderState()
{
	RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket();
	if (!packet)
		return;
	RenderWorld::STerritory& territory = packet->territory.emplace();
	if (m_LinesPrepared)
	{
		territory.lines = std::move(m_PreparedBoundaryLines);
		territory.debugNodes = std::move(m_PreparedDebugBoundaryLineNodes);
		m_PreparedBoundaryLines.clear();
		m_PreparedDebugBoundaryLineNodes.clear();
		m_LinesPrepared = false;

		if (m_DebugOverlayEnabled && m_Territories)
		{
			const float texelsPerTile{static_cast<float>(Pathfinding::NAVCELLS_PER_TERRAIN_TILE) /
				ICmpTerritoryManager::NAVCELLS_PER_TERRITORY_TILE};
			packet->terrainOverlays.push_back({RenderWorld::DebugSource::TERRITORY,
				RenderWorld::BuildTerrainOverlay(texelsPerTile, GetSimContext().GetTerrain().GetTilesPerSide(),
					[this](std::uint8_t* data, const std::size_t w, const std::size_t h)
					{
						BuildTextureRGBA(m_Territories, data, static_cast<int>(w), static_cast<int>(h));
					})});
		}
	}
	if (m_ColorsPrepared)
	{
		territory.colors = std::move(m_PreparedColors);
		m_PreparedColors.clear();
		m_ColorsPrepared = false;
	}
	territory.visible = IsVisible();
}
