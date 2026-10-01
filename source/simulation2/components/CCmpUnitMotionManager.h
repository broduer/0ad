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

#ifndef INCLUDED_CCMPUNITMOTIONMANAGER
#define INCLUDED_CCMPUNITMOTIONMANAGER

#include "graphics/Color.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/components/ICmpUnitMotionManager.h"
#include "simulation2/components/ICmpPhysicalBody.h"
#include "simulation2/components/ICmpPathfinder.h"
#include "simulation2/helpers/Grid.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/helpers/PushConfig.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/EntityMap.h"
#include "simulation2/helpers/MotionState.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class CCmpUnitMotion;
class ICmpPosition;
class ICmpPhysicalBody;

class CCmpUnitMotionManager final : public ICmpUnitMotionManager
{
public:
	static void ClassInit(CComponentManager& componentManager);

	DEFAULT_COMPONENT_ALLOCATOR(UnitMotionManager)

	// These vectors are reconstructed on deserialization.

	EntityMap<MotionState> m_Units;
	EntityMap<MotionState> m_FormationControllers;

	// Turn-local state below, not serialised.

	bool m_ComputingMotion;

	// The spatial grids of the pushing pass. Only cleared between turns, not reallocated.
	UnitCollision::SpatialGrids m_PushGrids;

	// The solid contacts of the pushing pass, grouped by unit: each MotionState views its own.
	std::vector<MomentumModel::Contact> m_Contacts;

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	void Init(const CParamNode&) override;

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serialize) override;
	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override;

	void HandleMessage(const CMessage& msg, bool global) override;

	void Register(CCmpUnitMotion* component, ICmpPhysicalBody* physicalBody, entity_id_t ent, bool formationController) override;
	void Unregister(entity_id_t ent) override;

	bool ComputingMotion() const override
	{
		return m_ComputingMotion;
	}

	bool IsPushingActivated() const override
	{
		return PushConfig::GetInstance().IsEnabled();
	}

	MotionState* GetMotionState(entity_id_t ent)
	{
		auto it = m_Units.find(ent);
		if (it != m_Units.end())
			return &it->second;

		it = m_FormationControllers.find(ent);
		if (it != m_FormationControllers.end())
			return &it->second;

		return nullptr;
	}

private:
	void OnDeserialized();
	void OnTurnStart();

	void MoveUnits(fixed dt);
	void MoveFormations(fixed dt);
	void Move(EntityMap<MotionState>& ents, fixed dt);

	// Debug rendering, only defined when DEBUG_RENDER is enabled in CCmpUnitMotion_System.cpp.
	void RenderPushGridCell(const UnitCollision::SpatialGridTier& tier, int cellX, int cellZ, fixed height,
						const CColor& color);
	void RenderForceVector(const MotionState& state,
						const CFixedVector2D& force,
						bool wasObstructed);
	void ClearDebugData();
};

REGISTER_COMPONENT_TYPE(UnitMotionManager)

#endif // INCLUDED_CCMPUNITMOTIONMANAGER
