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

#ifndef INCLUDED_FRAMEPACKET
#define INCLUDED_FRAMEPACKET

#include "graphics/Color.h"
#include "graphics/ModelAbstract.h"
#include "graphics/Overlay.h"
#include "lib/file/vfs/vfs_path.h"
#include "maths/FixedVector3D.h"
#include "maths/Vector3D.h"
#include "ps/CStr.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/system/Entity.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

enum class LosVisibility : std::uint8_t;
struct SViewFrame;

namespace RenderWorld
{

enum class AnchorType : std::uint8_t
{
	UPRIGHT,
	PITCH,
	ROLL,
	PITCH_ROLL,
	UNDEFINED
};

inline AnchorType ParseAnchorType(const CStr& anchor)
{
	if (anchor == "pitch")
		return AnchorType::PITCH;
	if (anchor == "roll")
		return AnchorType::ROLL;
	if (anchor == "pitch-roll")
		return AnchorType::PITCH_ROLL;
	return AnchorType::UPRIGHT;
}

struct SAnimation
{
	std::string name{"idle"};
	bool once{false};
	float speed{1.0f};
	float desync{0.0f};
	std::wstring soundGroup;
};

/**
 * Everything the render world needs to build an entity's unit.
 */
struct SVisual
{
	std::wstring actorName;
	std::uint32_t seed{0};
	std::map<CStr, CStr> selections;
	std::optional<CModelAbstract::CustomSelectionShape> selectionShape;
	// ModelFlag bits added to the whole model tree.
	std::uint32_t modelFlags{0};
	bool shadowsCast{true};
	bool shadowsReceive{true};
	bool actorOnly{false};
	bool visibleInAtlasOnly{false};
	player_id_t player{INVALID_PLAYER};
	CColor shading{1.0f, 1.0f, 1.0f, 1.0f};
	SAnimation animation;
	float syncRepeat{0.0f};
	float syncOffset{0.0f};
};

namespace Visual
{
// Create the entity's unit from the actor, seed, shape and flags.
struct Create { entity_id_t entity; SVisual visual; };
// Replace the entity's unit for a new actor or seed, keeping the rest of the visual.
struct Rebuild { entity_id_t entity; SVisual visual; };
// Reapply the selections and animation to the entity's unit.
struct ReloadAnimation { entity_id_t entity; SVisual visual; };
struct Destroy { entity_id_t entity; };
struct SetSelection { entity_id_t entity; CStr key; CStr selection; };
struct SetAnimation { entity_id_t entity; SAnimation animation; };
struct SetSyncRepeat { entity_id_t entity; float time; };
struct SetSyncOffset { entity_id_t entity; float time; };
struct SetShading { entity_id_t entity; CColor color; };
struct SetPlayer { entity_id_t entity; player_id_t player; };
struct SetVariable { entity_id_t entity; std::string name; float value; };
struct SetVisibility { entity_id_t entity; LosVisibility visibility; };
} // namespace Visual

using VisualRecord = std::variant<Visual::Create, Visual::Rebuild, Visual::ReloadAnimation,
	Visual::Destroy, Visual::SetSelection,
	Visual::SetAnimation, Visual::SetSyncRepeat, Visual::SetSyncOffset, Visual::SetShading,
	Visual::SetPlayer, Visual::SetVariable, Visual::SetVisibility>;

/**
 * An entity's position as the simulation last set it, with the events since
 * its previous record that move the render world's interpolated rotation.
 */
struct SPosition
{
	entity_id_t entity{INVALID_ENTITY};
	bool inWorld{false};
	float x{0.0f}, z{0.0f}, lastX{0.0f}, lastZ{0.0f};
	float y{0.0f}, lastYDifference{0.0f};
	bool relativeToGround{true};
	bool floating{false};
	float floatDepth{0.0f};
	float constructionProgress{0.0f};
	float rotX{0.0f}, rotY{0.0f}, rotZ{0.0f}, rotYSpeed{0.0f};
	AnchorType anchorType{AnchorType::UPRIGHT};
	entity_id_t turretParent{INVALID_ENTITY};
	CVector3D turretPosition;
	// A turn started: the interpolated tilt restarts from its current value.
	bool turnStarted{false};
	// With turnStarted: the entity moved in the previous turn, so its tilt follows the terrain
	// under the new position.
	bool turnMoved{false};
	bool snapRotY{false};
	bool snapRotXZ{false};
};

/**
 * A selectable entity's overlay as the simulation last set it; a changed
 * target alpha starts a fade in the render world.
 */
struct SSelection
{
	entity_id_t entity{INVALID_ENTITY};
	SOverlayDescriptor descriptor;
	// Known once the entity has a footprint.
	bool shapeKnown{false};
	// A square rather than a circle.
	bool square{false};
	// Width and depth; for a circle, twice the radius.
	float width{0.0f}, height{0.0f};
	CFixedVector3D rotation;
	CColor color{0.0f, 0.0f, 0.0f, 0.0f};
	float targetAlpha{0.0f};
	bool visible{false};
	bool staticOverlayStale{false};
};

struct SSprite
{
	VfsPath texture;
	float x0, y0, x1, y1;
	CColor color;
	CVector3D offset;
};

struct STerritoryLine
{
	bool blinking;
	player_id_t owner;
	CColor color;
	SOverlayTexturedLine overlay;
};

/**
 * A terrain-texture debug overlay's pixels, as built on the logic thread.
 */
struct STerrainOverlay
{
	float texelsPerTile{1.0f};
	std::size_t width{0}, height{0};
	std::vector<std::uint8_t> rgba;
};

/**
 * Build a terrain overlay's pixels for a map @p tilesPerSide tiles across;
 * @p build fills width * height RGBA texels.
 */
template<typename Build>
std::shared_ptr<const STerrainOverlay> BuildTerrainOverlay(const float texelsPerTile, const std::ptrdiff_t tilesPerSide, Build&& build)
{
	std::shared_ptr<STerrainOverlay> overlay{std::make_shared<STerrainOverlay>()};
	overlay->texelsPerTile = texelsPerTile;
	overlay->width = overlay->height = static_cast<std::size_t>(tilesPerSide * texelsPerTile);
	overlay->rgba.assign(overlay->width * overlay->height * 4, 0);
	build(overlay->rgba.data(), overlay->width, overlay->height);
	return overlay;
}

/**
 * Where the displayed player sees, by LOS tile, for the projectiles the render
 * world places itself.
 */
struct SLosGrid
{
	std::size_t verticesPerSide{0};
	bool revealAll{false};
	std::vector<std::uint8_t> visible;

	bool IsVisible(const CVector3D& position) const;
};

namespace Overlay
{
struct SetSelection { SSelection selection; };
struct RemoveSelection { entity_id_t entity; };
struct SetSprites { entity_id_t entity; std::vector<SSprite> sprites; LosVisibility visibility; };
struct SetRanges { entity_id_t entity; std::vector<SOverlayDescriptor> descriptors; CColor color; };
struct SetRallyPoint
{
	entity_id_t entity;
	std::vector<std::vector<SOverlayTexturedLine>> lines;
	std::vector<std::vector<SOverlayLine>> debug;
	bool shown;
	bool debugEnabled;
};
// Remove an entity's sprites, ranges and rally point.
struct RemoveEntity { entity_id_t entity; };
} // namespace Overlay

using OverlayRecord = std::variant<Overlay::SetSelection, Overlay::RemoveSelection, Overlay::SetSprites,
	Overlay::SetRanges, Overlay::SetRallyPoint, Overlay::RemoveEntity>;

/**
 * Where debug lines come from; each source's latest lines replace its
 * previous ones.
 */
enum class DebugSource : std::uint8_t
{
	RANGE_MANAGER,
	OBSTRUCTION_MANAGER,
	HIERARCHICAL_PATHFINDER,
	VERTEX_PATHFINDER,
	UNIT_MOTION,
	TERRITORY,
	LONG_PATHFINDER,
	ATLAS_PASSABILITY
};

struct SDebugLines
{
	DebugSource source;
	// For per-entity sources; INVALID_ENTITY otherwise.
	entity_id_t entity{INVALID_ENTITY};
	std::vector<SOverlayLine> lines;
	std::vector<SOverlayTexturedLine> texturedLines;
};

struct STerrainOverlayRecord
{
	DebugSource source;
	// Null removes the overlay.
	std::shared_ptr<const STerrainOverlay> overlay;
};

namespace Projectile
{
struct Launch
{
	std::uint32_t id;
	CVector3D origin;
	CVector3D target;
	float speed;
	float gravity;
	std::wstring actorName;
	std::wstring impactActorName;
	float impactAnimationLifetime;
	std::uint32_t seed;
};
struct Remove { std::uint32_t id; };
} // namespace Projectile

using ProjectileRecord = std::variant<Projectile::Launch, Projectile::Remove>;

struct STerritory
{
	std::optional<std::vector<STerritoryLine>> lines;
	std::optional<std::vector<SOverlayLine>> debugNodes;
	std::optional<std::map<player_id_t, CColor>> colors;
	bool visible{false};
};

/**
 * What the simulation tells the render world, one packet per logic step. The render thread
 * applies the packets in order; the logic thread doesn't touch a packet once handed over, and
 * nothing in it refers to logic-owned memory.
 */
struct CFramePacket
{
	std::vector<VisualRecord> visuals;
	std::vector<SPosition> positions;
	std::vector<entity_id_t> removedPositions;
	std::optional<float> waterLevel;
	// The terrain's new size, for the water, after a resize.
	std::optional<std::ptrdiff_t> waterMapSize;
	// The water level or the terrain under it changed: recompute the water
	// data and all of the terrain's render data.
	bool waterChanged{false};
	struct STerrainRegion { std::ptrdiff_t i0, j0, i1, j1; };
	// Tiles whose heights changed, for their render data.
	std::vector<STerrainRegion> dirtyTerrain;
	// A simulation turn started: every rotating entity restarts its tilt interpolation, as
	// SPosition::turnStarted does for one entity.
	bool turnStarted{false};
	std::optional<bool> unitDebugOverlay;
	std::optional<bool> selectionDebugOverlay;
	std::optional<bool> particlesUseSimTime;

	std::vector<OverlayRecord> overlays;
	std::vector<ProjectileRecord> projectiles;
	std::optional<STerritory> territory;
	std::vector<SDebugLines> debugLines;
	std::vector<STerrainOverlayRecord> terrainOverlays;
	std::shared_ptr<const SLosGrid> los;

	// The step's interpolation lengths and offset, and how far the renderer's clocks advance.
	struct STiming
	{
		float simFrameLength{0.0f};
		float offset{0.0f};
		float realFrameLength{0.0f};
		float timeManagerDelta{0.0f};
		float realTimeDelta{0.0f};
		bool paused{false};
	};
	// Set by frame steps only.
	std::optional<STiming> timing;
	std::optional<std::vector<CColor>> playerColors;
	// The view's cameras and textures as of the step; null outside a game.
	std::shared_ptr<SViewFrame> view;

	void Clear()
	{
		visuals.clear();
		positions.clear();
		removedPositions.clear();
		waterLevel.reset();
		waterMapSize.reset();
		waterChanged = false;
		dirtyTerrain.clear();
		turnStarted = false;
		unitDebugOverlay.reset();
		selectionDebugOverlay.reset();
		particlesUseSimTime.reset();
		overlays.clear();
		projectiles.clear();
		territory.reset();
		debugLines.clear();
		terrainOverlays.clear();
		los.reset();
		timing.reset();
		playerColors.reset();
		view.reset();
	}
};

} // namespace RenderWorld

#endif // INCLUDED_FRAMEPACKET
