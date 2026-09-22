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

#include "ICmpUnitRenderer.h"

#include "graphics/FramePacket.h"
#include "graphics/RenderWorld.h"
#include "maths/BoundingBoxAligned.h"
#include "maths/BoundingBoxOriented.h"
#include "maths/Vector3D.h"
#include "ps/Profile.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/components/ICmpSelectable.h"
#include "simulation2/components/ICmpVisibility.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Message.h"
#include "simulation2/system/SimContext.h"

#include <string>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * Keeps the LOS visibility of the units the render world draws up to date, and
 * picks units by what the render world last drew.
 *
 * Visibility is computed on the logic thread, as it needs the range manager and
 * may call script. It is recomputed for every unit once per turn and for units
 * that moved in between; the render world is told only of changes.
 */
class CCmpUnitRenderer final : public ICmpUnitRenderer
{
public:
	struct SUnit
	{
		CEntityHandle entity;
		LosVisibility visibility = LosVisibility::HIDDEN;
		bool visibilityDirty = true;
		// The Visibility component's flag, read on the first update.
		std::optional<bool> alwaysVisible;
	};

	std::vector<SUnit> m_Units;
	std::unordered_map<entity_id_t, size_t> m_Index;

	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_TurnStart);
		componentManager.SubscribeToMessageType(MT_FramePrepare);
	}

	DEFAULT_COMPONENT_ALLOCATOR(UnitRenderer)

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer&) override
	{
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer&) override
	{
		Init(paramNode);
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_TurnStart:
		{
			// Visibility must be recomputed on the first frame during this turn.
			for (SUnit& unit : m_Units)
				unit.visibilityDirty = true;
			break;
		}
		case MT_FramePrepare:
		{
			UpdateVisibilities();
			break;
		}
		}
	}

	void AddUnit(CEntityHandle entity) override
	{
		const auto [it, inserted] = m_Index.emplace(entity.GetId(), m_Units.size());
		if (!inserted)
		{
			m_Units[it->second].visibilityDirty = true;
			return;
		}
		m_Units.push_back(SUnit{entity});
	}

	void RemoveUnit(const entity_id_t entity) override
	{
		const auto it = m_Index.find(entity);
		if (it == m_Index.end())
			return;
		const size_t index = it->second;
		m_Index.erase(it);
		if (index + 1 != m_Units.size())
		{
			m_Units[index] = std::move(m_Units.back());
			m_Index[m_Units[index].entity.GetId()] = index;
		}
		m_Units.pop_back();
	}

	void MarkVisibilityDirty(const entity_id_t entity) override
	{
		const auto it = m_Index.find(entity);
		if (it != m_Index.end())
			m_Units[it->second].visibilityDirty = true;
	}

	void SetDebugOverlay(bool enabled) override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->unitDebugOverlay = enabled;
	}

	void PickAllEntitiesAtPoint(std::vector<std::pair<CEntityHandle, CVector3D> >& outEntities, const CVector3D& origin, const CVector3D& dir, bool allowEditorSelectables) const override
	{
		const RenderWorld::SFeedback* feedback = GetSimContext().GetRenderFeedback();
		if (!feedback)
			return;
		float tmin, tmax;
		CVector3D center;
		for (const RenderWorld::SUnitFeedback& unit : feedback->units)
		{
			if (!unit.inWorld)
				continue;
			if (!unit.sweptBounds.RayIntersect(origin, dir))
				continue;
			if (unit.selectionBox.IsEmpty())
			{
				if (!allowEditorSelectables)
					continue;
				if (unit.bounds.IsEmpty())
					continue;
				if (!unit.bounds.RayIntersect(origin, dir, tmin, tmax))
					continue;
				unit.bounds.GetCenter(center);
			}
			else
			{
				if (!unit.selectionBox.RayIntersect(origin, dir, tmin, tmax))
					continue;
				center = unit.selectionBox.m_Center;
			}
			// The render world may still report an entity destroyed since.
			const auto it = m_Index.find(unit.entity);
			if (it != m_Index.end())
				outEntities.emplace_back(m_Units[it->second].entity, center);
		}
	}

private:
	void UpdateVisibilities();
	void UpdateVisibility(SUnit& unit, const ICmpRangeManager& rangeManager, player_id_t player) const;
};

void CCmpUnitRenderer::UpdateVisibilities()
{
	PROFILE3("UnitRenderer::UpdateVisibilities");

	RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket();
	CmpPtr<ICmpRangeManager> cmpRangeManager(GetSystemEntity());
	if (!cmpRangeManager)
		return;
	const player_id_t player{GetSimContext().GetCurrentDisplayedPlayer()};
	for (SUnit& unit : m_Units)
	{
		if (!unit.visibilityDirty)
			continue;
		const LosVisibility previous = unit.visibility;
		UpdateVisibility(unit, *cmpRangeManager.operator->(), player);
		if (packet && unit.visibility != previous)
			packet->visuals.emplace_back(RenderWorld::Visual::SetVisibility{unit.entity.GetId(), unit.visibility});
	}
}

void CCmpUnitRenderer::UpdateVisibility(SUnit& unit, const ICmpRangeManager& rangeManager, const player_id_t player) const
{
	CmpPtr<ICmpPosition> cmpPosition(unit.entity);
	if (cmpPosition && cmpPosition->IsInWorld())
	{
		// The 'always visible' flag means we should always render the unit
		// (regardless of whether the LOS system thinks it's visible)
		if (!unit.alwaysVisible)
		{
			CmpPtr<ICmpVisibility> cmpVisibility(unit.entity);
			unit.alwaysVisible = cmpVisibility && cmpVisibility->GetAlwaysVisible();
		}
		if (*unit.alwaysVisible)
			unit.visibility = LosVisibility::VISIBLE;
		else
			unit.visibility = rangeManager.GetLosVisibility(unit.entity, player);
	}
	else
		unit.visibility = LosVisibility::HIDDEN;

	// Change the visibility of the visual actor's selectable if it has one.
	CmpPtr<ICmpSelectable> cmpSelectable(unit.entity);
	if (cmpSelectable)
		cmpSelectable->SetVisibility(unit.visibility != LosVisibility::HIDDEN);

	unit.visibilityDirty = false;
}

REGISTER_COMPONENT_TYPE(UnitRenderer)
