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

#ifndef INCLUDED_RENDEROVERLAYS
#define INCLUDED_RENDEROVERLAYS

#include "graphics/FramePacket.h"
#include "graphics/Overlay.h"
#include "lib/code_annotation.h"
#include "maths/BoundingBoxAligned.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

class CFrustum;
class SceneCollector;
class TerrainTextureOverlay;

namespace RenderWorld
{

class CRenderWorld;

/**
 * The render world's overlays: selection rings, sprites, range circles,
 * rally points, territory borders and debug lines, built from the packets'
 * records and drawn at the entities' interpolated positions. Entries are kept
 * by entity, so they are submitted in the same order in every run.
 */
class COverlays
{
	NONCOPYABLE(COverlays);
public:
	explicit COverlays(const CRenderWorld& world);
	~COverlays();

	void Apply(OverlayRecord& record);
	void ApplyTerritory(STerritory& territory);
	void ApplyDebugLines(SDebugLines& lines);
	void ApplyTerrainOverlay(const STerrainOverlayRecord& record);
	void SetSelectionDebug(bool enabled) { m_SelectionDebug = enabled; }

	void Interpolate(float simFrameLength, float realFrameLength);
	void Submit(SceneCollector& collector, const CFrustum& frustum, bool culling);

private:
	struct SSelectionState
	{
		SSelection sim;
		// The colour as drawn, and the fade in progress.
		CColor color{0.0f, 0.0f, 0.0f, 0.0f};
		float fadeBaselineAlpha{0.0f};
		float fadeDeltaAlpha{0.0f};
		float fadeProgress{0.0f};
		std::unique_ptr<SOverlayTexturedLine> building;
		std::unique_ptr<SOverlayQuad> unit;
		CBoundingBoxAligned unitBounds;
		std::unique_ptr<SOverlayLine> debugBounds;
		std::unique_ptr<SOverlayLine> debugSelection;
	};

	struct SSprites
	{
		std::vector<SOverlaySprite> sprites;
		std::vector<CVector3D> offsets;
		LosVisibility visibility{};
		bool enabled{false};
	};

	struct SRanges
	{
		std::vector<SOverlayDescriptor> descriptors;
		CColor color;
		std::vector<std::unique_ptr<SOverlayTexturedLine>> lines;
	};

	void ApplyRecord(Overlay::SetSelection& record);
	void ApplyRecord(Overlay::RemoveSelection& record);
	void ApplyRecord(Overlay::SetSprites& record);
	void ApplyRecord(Overlay::SetRanges& record);
	void ApplyRecord(Overlay::SetRallyPoint& record);
	void ApplyRecord(Overlay::RemoveEntity& record);

	static bool IsActive(const SSelectionState& state);
	void UpdateSelectionLine(entity_id_t entity, SSelectionState& state, SOverlayTexturedLine& line) const;
	void UpdateSelectionQuad(entity_id_t entity, SSelectionState& state) const;
	void SubmitSelection(entity_id_t entity, SSelectionState& state, SceneCollector& collector,
		const CFrustum& frustum, bool culling);

	const CRenderWorld& m_World;
	bool m_SelectionDebug{false};

	std::map<entity_id_t, SSelectionState> m_Selections;
	// The selections shown, fading or with debug boxes: the ones a frame
	// updates and submits.
	std::map<entity_id_t, SSelectionState*> m_ActiveSelections;
	std::map<entity_id_t, SSprites> m_Sprites;
	std::map<entity_id_t, SRanges> m_Ranges;
	std::map<entity_id_t, Overlay::SetRallyPoint> m_RallyPoints;

	std::vector<STerritoryLine> m_TerritoryLines;
	std::vector<SOverlayLine> m_TerritoryDebugNodes;
	bool m_TerritoryVisible{false};
	double m_TerritoryAnimTime{0.0};

	std::map<std::pair<DebugSource, entity_id_t>, SDebugLines> m_DebugLines;
	std::map<DebugSource, std::unique_ptr<TerrainTextureOverlay>> m_TerrainOverlays;
};

} // namespace RenderWorld

#endif // INCLUDED_RENDEROVERLAYS
