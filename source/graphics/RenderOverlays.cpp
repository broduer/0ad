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

#include "RenderOverlays.h"

#include "graphics/ModelAbstract.h"
#include "graphics/RenderWorld.h"
#include "graphics/Terrain.h"
#include "graphics/TextureManager.h"
#include "graphics/Unit.h"
#include "maths/Ease.h"
#include "maths/Frustum.h"
#include "maths/Matrix3D.h"
#include "maths/Vector2D.h"
#include "ps/Profile.h"
#include "renderer/Renderer.h"
#include "renderer/Scene.h"
#include "renderer/TerrainOverlay.h"
#include "renderer/backend/Sampler.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/helpers/Render.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace RenderWorld
{

namespace
{

constexpr float SELECTION_FADE_DURATION = 0.3f;

} // anonymous namespace

COverlays::COverlays(const CRenderWorld& world)
	: m_World{world}
{
}

COverlays::~COverlays() = default;

void COverlays::Apply(OverlayRecord& record)
{
	std::visit([this](auto& r) { ApplyRecord(r); }, record);
}

void COverlays::ApplyRecord(Overlay::SetSelection& record)
{
	const auto [it, inserted] = m_Selections.try_emplace(record.selection.entity);
	SSelectionState& state = it->second;
	const SSelection& sim = record.selection;
	state.color.r = sim.color.r;
	state.color.g = sim.color.g;
	state.color.b = sim.color.b;
	// A new overlay starts at its target, without fading in.
	if (inserted)
	{
		state.color.a = sim.targetAlpha;
		state.fadeBaselineAlpha = sim.targetAlpha;
	}
	else if (sim.targetAlpha != state.sim.targetAlpha)
	{
		state.fadeBaselineAlpha = state.color.a;
		state.fadeDeltaAlpha = sim.targetAlpha - state.fadeBaselineAlpha;
		state.fadeProgress = 0.0f;
	}
	if (sim.staticOverlayStale)
		state.building.reset();
	state.sim = sim;
	if (IsActive(state))
		m_ActiveSelections.insert_or_assign(sim.entity, &state);
}

void COverlays::ApplyRecord(Overlay::RemoveSelection& record)
{
	m_ActiveSelections.erase(record.entity);
	m_Selections.erase(record.entity);
}

bool COverlays::IsActive(const SSelectionState& state)
{
	return state.color.a > 0 || state.fadeDeltaAlpha != 0.0f || state.fadeBaselineAlpha > 0;
}

void COverlays::ApplyRecord(Overlay::SetSprites& record)
{
	if (record.sprites.empty())
	{
		m_Sprites.erase(record.entity);
		return;
	}
	SSprites& sprites = m_Sprites[record.entity];
	sprites.sprites.clear();
	sprites.offsets.clear();
	for (const SSprite& sprite : record.sprites)
	{
		SOverlaySprite& overlay = sprites.sprites.emplace_back();
		overlay.m_Texture = g_Renderer.GetTextureManager().CreateTexture(CTextureProperties(sprite.texture));
		overlay.m_X0 = sprite.x0;
		overlay.m_Y0 = sprite.y0;
		overlay.m_X1 = sprite.x1;
		overlay.m_Y1 = sprite.y1;
		overlay.m_Color = sprite.color;
		sprites.offsets.push_back(sprite.offset);
	}
	sprites.visibility = record.visibility;
}

void COverlays::ApplyRecord(Overlay::SetRanges& record)
{
	if (record.descriptors.empty())
	{
		m_Ranges.erase(record.entity);
		return;
	}
	SRanges& ranges = m_Ranges[record.entity];
	ranges.descriptors = std::move(record.descriptors);
	ranges.color = record.color;
	ranges.lines.clear();
	ranges.lines.resize(ranges.descriptors.size());
}

void COverlays::ApplyRecord(Overlay::SetRallyPoint& record)
{
	for (std::vector<SOverlayTexturedLine>& row : record.lines)
		for (SOverlayTexturedLine& line : row)
			line.m_Ground = &m_World;
	m_RallyPoints[record.entity] = std::move(record);
}

void COverlays::ApplyRecord(Overlay::RemoveEntity& record)
{
	m_Sprites.erase(record.entity);
	m_Ranges.erase(record.entity);
	m_RallyPoints.erase(record.entity);
	for (auto it = m_DebugLines.lower_bound({DebugSource::UNIT_MOTION, record.entity});
		it != m_DebugLines.end() && it->first == std::make_pair(DebugSource::UNIT_MOTION, record.entity);)
		it = m_DebugLines.erase(it);
}

void COverlays::ApplyTerritory(STerritory& territory)
{
	if (territory.lines)
	{
		m_TerritoryLines = std::move(*territory.lines);
		for (STerritoryLine& line : m_TerritoryLines)
			line.overlay.m_Ground = &m_World;
	}
	if (territory.debugNodes)
		m_TerritoryDebugNodes = std::move(*territory.debugNodes);
	if (territory.colors)
	{
		for (STerritoryLine& line : m_TerritoryLines)
		{
			const auto color = territory.colors->find(line.owner);
			if (color == territory.colors->end())
				continue;
			line.color = color->second;
			line.overlay.m_Color = line.color;
		}
	}
	m_TerritoryVisible = territory.visible;
}

void COverlays::ApplyDebugLines(SDebugLines& lines)
{
	const std::pair<DebugSource, entity_id_t> key{lines.source, lines.entity};
	if (lines.lines.empty() && lines.texturedLines.empty())
	{
		m_DebugLines.erase(key);
		return;
	}
	for (SOverlayTexturedLine& line : lines.texturedLines)
		line.m_Ground = &m_World;
	m_DebugLines[key] = std::move(lines);
}

void COverlays::ApplyTerrainOverlay(const STerrainOverlayRecord& record)
{
	if (!record.overlay)
	{
		m_TerrainOverlays.erase(record.source);
		return;
	}
	std::unique_ptr<TerrainTextureOverlay>& overlay = m_TerrainOverlays[record.source];
	if (!overlay)
		overlay = std::make_unique<TerrainTextureOverlay>(record.overlay->texelsPerTile);
	overlay->SetData(record.overlay);
}

void COverlays::UpdateSelectionLine(const entity_id_t entity, SSelectionState& state, SOverlayTexturedLine& line) const
{
	if (!CRenderer::IsInitialised())
		return;
	if (!m_World.IsInWorld(entity) || !state.sim.shapeKnown)
		return;

	float rotY;
	CVector2D origin;
	m_World.GetInterpolatedPosition2D(entity, origin.X, origin.Y, rotY);

	line.m_Ground = &m_World;
	line.m_Color = state.color;
	line.CreateOverlayTexture(&state.sim.descriptor);

	if (state.sim.square)
		SimRender::ConstructTexturedLineBox(line, origin, state.sim.rotation, state.sim.width, state.sim.height);
	else
		SimRender::ConstructTexturedLineCircle(line, origin, state.sim.width);
}

void COverlays::UpdateSelectionQuad(const entity_id_t entity, SSelectionState& state) const
{
	// The quad is rebuilt at every frame, as units move constantly.
	if (state.sim.descriptor.m_Type != DYNAMIC_QUAD)
		return;
	if (!CRenderer::IsInitialised())
		return;
	if (!m_World.IsInWorld(entity) || !state.sim.shapeKnown)
		return;

	float rotY;
	CVector2D position;
	m_World.GetInterpolatedPosition2D(entity, position.X, position.Y, rotY);

	if (!state.unit)
	{
		state.unit = std::make_unique<SOverlayQuad>();

		CTextureProperties texturePropsBase(state.sim.descriptor.m_QuadTexture.c_str());
		texturePropsBase.SetAddressMode(
			Renderer::Backend::Sampler::AddressMode::CLAMP_TO_BORDER,
			Renderer::Backend::Sampler::AddressMode::CLAMP_TO_EDGE);
		texturePropsBase.SetAnisotropicFilter(true);

		CTextureProperties texturePropsMask(state.sim.descriptor.m_QuadTextureMask.c_str());
		texturePropsMask.SetAddressMode(
			Renderer::Backend::Sampler::AddressMode::CLAMP_TO_BORDER,
			Renderer::Backend::Sampler::AddressMode::CLAMP_TO_EDGE);
		texturePropsMask.SetAnisotropicFilter(true);

		state.unit->m_Texture = g_Renderer.GetTextureManager().CreateTexture(texturePropsBase);
		state.unit->m_TextureMask = g_Renderer.GetTextureManager().CreateTexture(texturePropsMask);
	}

	state.unit->m_Color = state.color;

	const float s{std::sin(-rotY)};
	const float c{std::cos(-rotY)};
	const CVector2D unitX{c, s};
	const CVector2D unitZ{-s, c};

	float halfSizeX{state.sim.width};
	float halfSizeZ{state.sim.height};
	if (state.sim.square)
	{
		halfSizeX /= 2.0f;
		halfSizeZ /= 2.0f;
	}

	const CVector2D points[4]{
		position + unitX * (-halfSizeX) + unitZ * halfSizeZ, // top left
		position + unitX * (-halfSizeX) + unitZ * (-halfSizeZ), // bottom left
		position + unitX * halfSizeX + unitZ * (-halfSizeZ), // bottom right
		position + unitX * halfSizeX + unitZ * halfSizeZ // top right
	};

	state.unitBounds = CBoundingBoxAligned();
	for (size_t i = 0; i < 4; ++i)
	{
		const float quadY{std::max(m_World.GetExactGroundLevel(points[i].X, points[i].Y), m_World.GetWaterLevel())};
		state.unit->m_Corners[i] = CVector3D(points[i].X, quadY, points[i].Y);
		state.unitBounds += state.unit->m_Corners[i];
	}
}

void COverlays::Interpolate(const float simFrameLength, const float realFrameLength)
{
	PROFILE3("render overlays interpolate");

	for (auto it = m_ActiveSelections.begin(); it != m_ActiveSelections.end();)
	{
		SSelectionState& state = *it->second;
		if (state.fadeDeltaAlpha != 0.0f)
		{
			state.fadeProgress += realFrameLength;
			if (state.fadeProgress >= SELECTION_FADE_DURATION)
			{
				const float targetAlpha{state.fadeBaselineAlpha + state.fadeDeltaAlpha};
				state.color.a = targetAlpha;
				state.fadeBaselineAlpha = targetAlpha;
				state.fadeDeltaAlpha = 0.0f;
				// Reset to start the next fade.
				state.fadeProgress = SELECTION_FADE_DURATION;
			}
			else
				state.color.a = Ease::QuartOut(state.fadeProgress, state.fadeBaselineAlpha, state.fadeDeltaAlpha, SELECTION_FADE_DURATION);
		}
		if (state.color.a > 0)
			UpdateSelectionQuad(it->first, state);
		if (IsActive(state))
			++it;
		else
			it = m_ActiveSelections.erase(it);
	}

	for (std::pair<const entity_id_t, SSprites>& entry : m_Sprites)
	{
		SSprites& sprites = entry.second;
		CMatrix3D transform;
		sprites.enabled = sprites.visibility != LosVisibility::HIDDEN && m_World.IsInWorld(entry.first) &&
			m_World.GetInterpolatedTransform(entry.first, transform);
		if (!sprites.enabled)
			continue;
		const CVector3D position{transform.GetTranslation()};
		for (size_t i = 0; i < sprites.sprites.size(); ++i)
			sprites.sprites[i].m_Position = position + sprites.offsets[i];
	}

	if (CRenderer::IsInitialised())
	{
		for (std::pair<const entity_id_t, SRanges>& entry : m_Ranges)
		{
			SRanges& ranges = entry.second;
			if (!m_World.IsInWorld(entry.first))
				continue;
			float rotY;
			CVector2D origin;
			m_World.GetInterpolatedPosition2D(entry.first, origin.X, origin.Y, rotY);
			for (size_t i = 0; i < ranges.descriptors.size(); ++i)
			{
				std::unique_ptr<SOverlayTexturedLine>& line = ranges.lines[i];
				line = std::make_unique<SOverlayTexturedLine>();
				line->m_Ground = &m_World;
				line->m_Color = ranges.color;
				line->CreateOverlayTexture(&ranges.descriptors[i]);
				SimRender::ConstructTexturedLineCircle(*line, origin, static_cast<float>(ranges.descriptors[i].m_Radius));
			}
		}
	}

	m_TerritoryAnimTime += simFrameLength;
	for (STerritoryLine& line : m_TerritoryLines)
	{
		if (!line.blinking)
			continue;
		CColor c = line.color;
		// TODO: should let artists tweak this
		c.a *= 0.2f + 0.8f * std::fabs(static_cast<float>(std::cos(m_TerritoryAnimTime * std::numbers::pi)));
		line.overlay.m_Color = c;
	}
}

void COverlays::SubmitSelection(const entity_id_t entity, SSelectionState& state, SceneCollector& collector,
	const CFrustum& frustum, const bool culling)
{
	if (state.sim.visible && state.color.a > 0)
	{
		switch (state.sim.descriptor.m_Type)
		{
		case STATIC_OUTLINE:
		{
			// Static overlays are built once and kept until the simulation
			// marks them stale, as they rarely change.
			if (!state.building)
			{
				state.building = std::make_unique<SOverlayTexturedLine>();
				UpdateSelectionLine(entity, state, *state.building);
			}
			// Done separately so alpha changes don't require a full update.
			state.building->m_Color = state.color;
			if (culling && !state.building->IsVisibleInFrustum(frustum))
				break;
			collector.Submit(state.building.get());
			break;
		}
		case DYNAMIC_QUAD:
		{
			if (culling && !frustum.IsBoxVisible(state.unitBounds))
				break;
			if (state.unit)
				collector.Submit(state.unit.get());
			break;
		}
		default:
			break;
		}
	}

	// The debug boxes follow the target alpha, so they respond immediately to
	// deselection without waiting for the fade.
	if (state.fadeBaselineAlpha + state.fadeDeltaAlpha <= 0)
		return;
	if (!m_SelectionDebug)
	{
		state.debugBounds.reset();
		state.debugSelection.reset();
		return;
	}
	CUnit* unit{m_World.GetUnit(entity)};
	if (!unit)
		return;
	if (!state.debugBounds)
		state.debugBounds = std::make_unique<SOverlayLine>();
	if (!state.debugSelection)
		state.debugSelection = std::make_unique<SOverlayLine>();
	CModelAbstract& model = unit->GetModel();
	SimRender::ConstructBoxOutline(model.GetWorldBounds(), *state.debugBounds);
	state.debugBounds->m_Thickness = 0.1f;
	state.debugBounds->m_Color = CColor(1.0f, 0.0f, 0.0f, 1.0f);
	SimRender::ConstructBoxOutline(model.GetSelectionBox(), *state.debugSelection);
	state.debugSelection->m_Thickness = 0.1f;
	state.debugSelection->m_Color = CColor(0.0f, 1.0f, 0.0f, 1.0f);
	collector.Submit(state.debugBounds.get());
	collector.Submit(state.debugSelection.get());
}

void COverlays::Submit(SceneCollector& collector, const CFrustum& frustum, const bool culling)
{
	PROFILE3("render overlays submit");

	for (const std::pair<const entity_id_t, SSelectionState*>& entry : m_ActiveSelections)
		SubmitSelection(entry.first, *entry.second, collector, frustum, culling);

	for (std::pair<const entity_id_t, SSprites>& entry : m_Sprites)
		if (entry.second.enabled)
			for (SOverlaySprite& sprite : entry.second.sprites)
				collector.Submit(&sprite);

	for (std::pair<const entity_id_t, SRanges>& entry : m_Ranges)
		for (const std::unique_ptr<SOverlayTexturedLine>& line : entry.second.lines)
			if (line && (!culling || line->IsVisibleInFrustum(frustum)))
				collector.Submit(line.get());

	for (std::pair<const entity_id_t, Overlay::SetRallyPoint>& entry : m_RallyPoints)
	{
		Overlay::SetRallyPoint& rallyPoint = entry.second;
		if (!rallyPoint.shown)
			continue;
		for (std::vector<SOverlayTexturedLine>& row : rallyPoint.lines)
			for (SOverlayTexturedLine& line : row)
				if (!line.m_Coords.empty() && (!culling || line.IsVisibleInFrustum(frustum)))
					collector.Submit(&line);
		if (rallyPoint.debugEnabled)
			for (std::vector<SOverlayLine>& row : rallyPoint.debug)
				for (SOverlayLine& line : row)
					if (!line.m_Coords.empty())
						collector.Submit(&line);
	}

	if (m_TerritoryVisible)
	{
		for (STerritoryLine& line : m_TerritoryLines)
			if (!culling || line.overlay.IsVisibleInFrustum(frustum))
				collector.Submit(&line.overlay);
		for (SOverlayLine& node : m_TerritoryDebugNodes)
			collector.Submit(&node);
	}

	for (std::pair<const std::pair<DebugSource, entity_id_t>, SDebugLines>& entry : m_DebugLines)
	{
		for (SOverlayLine& line : entry.second.lines)
			collector.Submit(&line);
		for (SOverlayTexturedLine& line : entry.second.texturedLines)
			collector.Submit(&line);
	}
}

} // namespace RenderWorld
