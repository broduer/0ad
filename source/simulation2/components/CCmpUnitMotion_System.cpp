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

#include "CCmpUnitMotion.h"

#include "lib/debug.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "maths/FixedVector3D.h"
#include "maths/MathUtil.h"
#include "ps/CLogger.h"
#include "ps/Profiler2.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/CCmpUnitMotionManager.h"
#include "simulation2/components/ICmpObstructionManager.h"
#include "simulation2/components/ICmpPathfinder.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpUnitMotion.h"
#include "simulation2/components/ICmpImpulseQueue.h"
#include "simulation2/helpers/UnitCollision.h"
#include "simulation2/helpers/PushConfig.h"
#include "simulation2/helpers/Grid.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/serialization/SerializeTemplates.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/EntityMap.h"
#include "simulation2/system/Message.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>
#include <map>
#include <utility>
#include <vector>

#define DEBUG_STATS 0
#define DEBUG_RENDER 0

#if DEBUG_RENDER
#include "maths/Frustum.h"

void RenderDebugOverlay(SceneCollector& collector, const CFrustum& frustum, bool culling);

struct SDebugData {
	std::vector<SOverlaySphere> m_Spheres;
	std::vector<SOverlayLine> m_Lines;
	std::vector<SOverlayQuad> m_Quads;
} debugDataMotionMgr;
#endif

MotionState::MotionState(
	ICmpPosition* cmpPos,
	CCmpUnitMotion* cmpMotion,
	ICmpPhysicalBody* cmpPhysicalBody)
	: cmpPosition(cmpPos)
	, cmpUnitMotion(cmpMotion)
	, cmpPhysicalBody(cmpPhysicalBody)
	, initialPos()
	, pos()
	, speed(fixed::Zero())
	, initialAngle(fixed::Zero())
	, angle(fixed::Zero())
	, controlGroup(INVALID_ENTITY)
	, ignore(false)
	, needUpdate(false)
	, wentStraight(false)
	, wasObstructed(false)
	, isMoving(false)
{
}

void CCmpUnitMotionManager::ClassInit(CComponentManager& componentManager)
{
	componentManager.SubscribeToMessageType(MT_Deserialized);
	componentManager.SubscribeToMessageType(MT_TurnStart);
	componentManager.SubscribeToMessageType(MT_Update_Final);
	componentManager.SubscribeToMessageType(MT_Update_MotionUnit);
	componentManager.SubscribeToMessageType(MT_Update_MotionFormation);
#if DEBUG_RENDER
	componentManager.SubscribeToMessageType(MT_RenderSubmit);
#endif
}

void CCmpUnitMotionManager::HandleMessage(const CMessage& msg, bool /*global*/)
{
	switch (msg.GetType())
	{
		case MT_TurnStart:
		{
			OnTurnStart();
			break;
		}
		case MT_Update_MotionFormation:
		{
			fixed dt = static_cast<const CMessageUpdate_MotionFormation&>(msg).turnLength;
			m_ComputingMotion = true;
			MoveFormations(dt);
			m_ComputingMotion = false;
			break;
		}
		case MT_Update_MotionUnit:
		{
			fixed dt = static_cast<const CMessageUpdate_MotionUnit&>(msg).turnLength;
			m_ComputingMotion = true;
			MoveUnits(dt);
			m_ComputingMotion = false;
			break;
		}
		case MT_Deserialized:
		{
			OnDeserialized();
			break;
		}
#if DEBUG_RENDER
		case MT_RenderSubmit:
		{
			const CMessageRenderSubmit& msgData = static_cast<const CMessageRenderSubmit&> (msg);
			RenderDebugOverlay(msgData.collector, msgData.frustum, msgData.culling);
			break;
		}
#endif
	}
}
void CCmpUnitMotionManager::Init(const CParamNode&)
{
	m_ComputingMotion = false;
	// Load the settings at the start of each game.
	PushConfig::GetInstance().Reload();
}

template<>
struct SerializeHelper<EntityMap<MotionState>>
{
	void operator()(ISerializer& serialize, const char* /*name*/,
		EntityMap<MotionState>& value)
	{
		// Serialize manually, we don't have a default-constructor for deserialization.
		Serializer(serialize, "size", static_cast<std::uint32_t>(value.size()));
		for (EntityMap<MotionState>::iterator it = value.begin(); it != value.end(); ++it)
			Serializer(serialize, "ent id", it->first);
	}

	void operator()(IDeserializer& deserialize, const char* /*name*/,
		EntityMap<MotionState>& value)
	{
		std::uint32_t units = 0;
		Serializer(deserialize, "size", units);
		for (std::uint32_t i = 0; i < units; ++i)
		{
			entity_id_t ent = INVALID_ENTITY;
			Serializer(deserialize, "ent id", ent);
			// Insert an invalid motion state, will be cleared up in MT_Deserialized.
			MotionState state(nullptr, nullptr, nullptr);
			value.insert(ent, state);
		}
	}
};

void CCmpUnitMotionManager::Serialize(ISerializer& serialize)
{
	Serializer(serialize, "m_Units", m_Units);
	Serializer(serialize, "m_FormationControllers", m_FormationControllers);
}

void CCmpUnitMotionManager::Deserialize(const CParamNode& paramNode, IDeserializer& deserialize)
{
	Init(paramNode);
	Serializer(deserialize, "m_Units", m_Units);
	Serializer(deserialize, "m_FormationControllers", m_FormationControllers);
}

/**
 * This deserialization process is rather ugly, but it's required to store some data in the motion states.
 * Ideally, the motion state would actually be CCmpUnitMotion themselves, but for data locality
 * (because our components are stored randomly on the heap right now) they're not.
 * If we ever change the simulation so that components could be registered by their managers and exposed,
 * then we could just use CCmpUnitMotion directly and clean this code uglyness.
 */
void CCmpUnitMotionManager::OnDeserialized()
{
	// Fetch the components now that they exist.
	// The rest of the data was already deserialized or will be reconstructed.
	for (EntityMap<MotionState>::iterator it = m_Units.begin(); it != m_Units.end(); ++it)
	{
		it->second.cmpPosition = static_cast<ICmpPosition*>(QueryInterface(GetSimContext(), it->first, IID_Position));
		// We can know for a fact that these are CCmpUnitMotion because those are the ones registering with us
		// (and to ensure that they pass a CCmpUnitMotion pointer when registering).
		it->second.cmpUnitMotion = static_cast<CCmpUnitMotion*>(static_cast<ICmpUnitMotion*>(QueryInterface(GetSimContext(), it->first, IID_UnitMotion)));
		it->second.cmpPhysicalBody = static_cast<ICmpPhysicalBody*>(QueryInterface(GetSimContext(), it->first, IID_PhysicalBody));
	}
	for (EntityMap<MotionState>::iterator it = m_FormationControllers.begin(); it != m_FormationControllers.end(); ++it)
	{
		it->second.cmpPosition = static_cast<ICmpPosition*>(QueryInterface(GetSimContext(), it->first, IID_Position));
		it->second.cmpUnitMotion = static_cast<CCmpUnitMotion*>(static_cast<ICmpUnitMotion*>(QueryInterface(GetSimContext(), it->first, IID_UnitMotion)));
		it->second.cmpPhysicalBody = static_cast<ICmpPhysicalBody*>(QueryInterface(GetSimContext(), it->first, IID_PhysicalBody));
	}
}

void CCmpUnitMotionManager::Register(CCmpUnitMotion* component, entity_id_t ent, bool formationController)
{
	MotionState state(static_cast<ICmpPosition*>(QueryInterface(GetSimContext(), ent, IID_Position)), component,
		static_cast<ICmpPhysicalBody*>(QueryInterface(GetSimContext(), ent, IID_PhysicalBody)));

	if (!formationController)
		m_Units.insert(ent, state);
	else
		m_FormationControllers.insert(ent, state);
}

void CCmpUnitMotionManager::Unregister(entity_id_t ent)
{
	EntityMap<MotionState>::iterator it = m_Units.find(ent);
	if (it != m_Units.end())
	{
		m_Units.erase(it);
		return;
	}
	it = m_FormationControllers.find(ent);
	if (it != m_FormationControllers.end())
		m_FormationControllers.erase(it);
}

void CCmpUnitMotionManager::OnTurnStart()
{
	for (EntityMap<MotionState>::value_type& data : m_FormationControllers)
		data.second.cmpUnitMotion->OnTurnStart();

	for (EntityMap<MotionState>::value_type& data : m_Units)
		data.second.cmpUnitMotion->OnTurnStart();
}

void CCmpUnitMotionManager::MoveUnits(fixed dt)
{
	Move(m_Units, dt);
}

void CCmpUnitMotionManager::MoveFormations(fixed dt)
{
	Move(m_FormationControllers, dt);
}

void CCmpUnitMotionManager::Move(EntityMap<MotionState>& ents, fixed dt)
{
#if DEBUG_RENDER
	ClearDebugData();
#endif
#if DEBUG_STATS
	double start = timer_Time();
#endif

	PROFILE2("MotionMgr_Move");

	CmpPtr<ICmpImpulseQueue> impulseQueue(GetSystemEntity());

	bool isUnitsPhase = (&ents == &m_Units);

	// ========================================
	// PRE-MOVE
	// ========================================
	// Reset each unit's motion state and collect it for collision pushing.
	std::vector<UnitCollision::MotionStateCompact> compactStates;
	std::vector<MotionState*> motionStates;

	{
		PROFILE2("MotionMgr_PreMove");
		for (EntityMap<MotionState>::iterator it = ents.begin(); it != ents.end(); ++it)
		{
			// Reset the queued push of every unit, including one out of the world, which is skipped below:
			// it mustn't keep last turn's push until it comes back.
			it->second.impulseForce = CFixedVector2D{};
			it->second.impulsePressure = 0;

			if (!it->second.cmpPosition->IsInWorld())
			{
				it->second.needUpdate = false;
				continue;
			}

			it->second.cmpUnitMotion->PreMove(it->second);
			it->second.initialPos = it->second.cmpPosition->GetPosition2D();
			it->second.initialAngle = it->second.cmpPosition->GetRotation().Y;
			it->second.pos = it->second.initialPos;
			it->second.speed = it->second.cmpUnitMotion->GetCurrentSpeed();
			// Always start from the rendered heading. PostMove leaves it equal to the motion state's angle, and rotations done
			// outside the motion state (FaceTowardsPointFromPos, script TurnTo) would otherwise leave that angle stale.
			it->second.angle = it->second.initialAngle;

			// Build compact state
			UnitCollision::MotionStateCompact compact;
			compact.entity = it->first;
			compact.position = it->second.pos;
			compact.predictedPosition = it->second.pos + (it->second.pos - it->second.cmpPosition->GetPreviousPosition2D());
			compact.clearance = it->second.cmpUnitMotion->GetUnitClearance();
			compact.isMoving = it->second.isMoving;
			compact.ignore = it->second.ignore;
			compact.controlGroup = it->second.controlGroup;
			compact.weight = it->second.cmpPhysicalBody ?
				it->second.cmpPhysicalBody->GetWeight() :
				entity_pos_t::FromInt(10);

			compactStates.push_back(compact);
			motionStates.push_back(&it->second);
		}
	}

	// ========================================
	// COLLISION PUSHES
	// ========================================
	if (isUnitsPhase && IsPushingActivated() && !compactStates.empty())
	{
		PROFILE2("MotionMgr_GenerateCollisionForces");

		std::vector<CFixedVector2D> collisionForces;
		std::vector<uint8_t> collisionPressures;
		UnitCollision::ComputePushes(compactStates, dt, m_PushGrid, m_PushGridFilledCells, collisionForces, collisionPressures);

		if (impulseQueue && !collisionForces.empty())
		{
			std::vector<ImpulseRequest> requests;
			requests.reserve(compactStates.size());
			for (size_t i = 0; i < compactStates.size(); ++i)
				if (!collisionForces[i].IsZero() && !compactStates[i].ignore)
					requests.emplace_back(
						compactStates[i].entity,
						collisionForces[i],
						collisionPressures[i]
					);
			if (!requests.empty())
				impulseQueue->AddImpulses(requests);
		}
	}

	// ========================================
	// CONSUME IMPULSES
	// ========================================
	// Add the queued impulses to each target's motion state (the pre-move step reset them).
	if (impulseQueue && isUnitsPhase)
	{
		PROFILE2("MotionMgr_ConsumeImpulses");
		std::vector<ImpulseRequest> allImpulses = impulseQueue->ConsumeImpulses();

		for (const auto& force : allImpulses)
		{
			EntityMap<MotionState>::iterator it{ents.find(force.target)};
			// Only a unit in the world moves this turn, so only those take the impulse.
			if (it == ents.end() || !it->second.cmpPosition->IsInWorld())
				continue;

			MotionState& target{it->second};
			target.impulseForce += force.force;
			target.impulsePressure = static_cast<uint8_t>(std::min<int>(PushConfig::MAX_PRESSURE,
				target.impulsePressure + force.pressureIncrease));
		}
	}

	// ========================================
	// PRESSURE DAMPING
	// ========================================
	// Dampen the queued pushes by the pushing pressure.
	// They are applied after each unit's own movement, below.
	if (IsPushingActivated())
	{
		PROFILE2("MotionMgr_ApplyForces");
		const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();

		for (MotionState* const state : motionStates)
		{
			const bool hasPressure{state->impulsePressure > 0};
			const bool hasSignificantForce{state->impulseForce.CompareLength(cfg.minimalPushing) > 0};

			// Apply pressure first
			if (hasPressure && state->cmpPhysicalBody)
				state->cmpPhysicalBody->AddPressure(state->impulsePressure);

			// Wake up units that received significant forces
			if (hasSignificantForce && !state->needUpdate)
			{
				const entity_pos_t MIN_FORCE_TO_WAKE{entity_pos_t::FromFraction(1, 10)};
				if (state->impulseForce.Length() > MIN_FORCE_TO_WAKE)
				{
					state->needUpdate = true;
					state->pos = state->initialPos;
					state->speed = state->cmpUnitMotion->GetCurrentSpeed();
					state->angle = state->initialAngle;
				}
			}

			if (!state->needUpdate || state->ignore || !state->cmpPhysicalBody || !hasSignificantForce)
			{
				state->impulseForce = CFixedVector2D{};
				continue;
			}

			// Pressure damping
			const uint8_t currentPressure{state->cmpPhysicalBody->GetPressure()};
			const uint8_t dampingCap{std::min<uint8_t>(PushConfig::MAX_PUSH_DAMPING_PRESSURE, currentPressure)};
			const fixed damping{entity_pos_t::FromInt(PushConfig::MAX_PRESSURE - dampingCap) / PushConfig::MAX_PRESSURE};
			// Queued impulses are per-turn displacements (weight ratio and dt already applied in
			// UnitCollision::ComputePairPush).
			state->impulseForce = state->impulseForce.Multiply(damping);
#if DEBUG_RENDER
			RenderForceVector(*state, state->impulseForce, state->wasObstructed);
#endif
		}
	}

	// ========================================
	// MOVE
	// ========================================
	{
		PROFILE2("MotionMgr_MoveLoop");
		const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();
		CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
		ENSURE(cmpPathfinder);
		// Each unit's move reads only its own state and the obstruction shapes, which PostMove updates
		// later, so it does not matter in what order the units are moved.
		for (MotionState* const state : motionStates)
		{
			if (state->needUpdate)
			{
				state->cmpUnitMotion->Move(*state, dt);
				ApplyPush(*state, cmpPathfinder);
			}

			// Decay pressure after moving
			if (IsPushingActivated() && state->cmpPhysicalBody && state->cmpPhysicalBody->GetPressure() > 0)
			{
				const uint8_t currentPressure{state->cmpPhysicalBody->GetPressure()};
				const entity_pos_t decayed{entity_pos_t::FromInt(currentPressure).Multiply(cfg.pressureDecay)};
				state->cmpPhysicalBody->SetPressure(static_cast<uint8_t>(decayed.ToInt_RoundToZero()));
			}
		}
	}

	// ========================================
	// POST-MOVE
	// ========================================
	{
		PROFILE2("MotionMgr_PostMove");
		for (auto& data : ents)
		{
			if (!data.second.needUpdate)
				continue;

			data.second.cmpUnitMotion->PostMove(data.second, dt);
		}
	}

#if DEBUG_STATS
	const double time{timer_Time() - start};
	if (!motionStates.empty())
		printf(">> Processed %zu units in %f secs\n", motionStates.size(), time);
#endif
}

void CCmpUnitMotionManager::ApplyPush(MotionState& state, CmpPtr<ICmpPathfinder> cmpPathfinder)
{
	if (state.impulseForce.IsZero() || !state.cmpPhysicalBody)
		return;

	CCmpUnitMotion& cmpUnitMotion{*state.cmpUnitMotion};
	const CFixedVector2D push{state.impulseForce};
	state.impulseForce = CFixedVector2D{};

	// If there was an attempt at movement, and we're getting pushed significantly and
	// away from where we'd like to go (measured by a low dot product)
	// then mark the unit as obstructed, but push anyways.
	// (this helps units stop earlier in many situations in a realistic-ish manner).
	if (state.pos != state.initialPos &&
		(state.pos - state.initialPos).Dot(state.pos + push - state.initialPos) < entity_pos_t::FromInt(1) / 2 &&
		state.cmpPhysicalBody->GetPressure() > 30)
	{
		state.wasObstructed = true;
		state.cmpPhysicalBody->SetPressure(std::max<uint8_t>(PushConfig::MIN_PRESSURE_IF_OBSTRUCTED,
			state.cmpPhysicalBody->GetPressure()));
		// Push anyways.
	}

	// Prevent pushed units from crossing uncrossable boundaries
	// (we can assume that normal movement didn't push units into impassable terrain).
	if (!cmpPathfinder->CheckMovement(cmpUnitMotion.GetObstructionFilter(), state.pos.X, state.pos.Y,
		state.pos.X + push.X, state.pos.Y + push.Y, cmpUnitMotion.m_Clearance, cmpUnitMotion.m_PassClass))
	{
		// Mark them as obstructed - this could possibly be optimised
		// perhaps it'd make more sense to mark the pushers as blocked.
		state.wasObstructed = true;
		state.wentStraight = false;
		return;
	}
	state.pos += push;
}

#if DEBUG_RENDER
void RenderDebugOverlay(SceneCollector& collector, const CFrustum& frustum, bool /*culling*/)
{
	for (SOverlaySphere& sph: debugDataMotionMgr.m_Spheres)
		if (frustum.IsSphereVisible(sph.m_Center, sph.m_Radius))
			collector.Submit(&sph);
	for (SOverlayLine& l: debugDataMotionMgr.m_Lines)
		if (frustum.IsPointVisible(l.m_Coords[0]) || frustum.IsPointVisible(l.m_Coords[1]))
			collector.Submit(&l);
	for (SOverlayQuad& quad: debugDataMotionMgr.m_Quads)
		collector.Submit(&quad);
}
#endif

#if DEBUG_RENDER
void CCmpUnitMotionManager::ClearDebugData()
{
	debugDataMotionMgr.m_Spheres.clear();
	debugDataMotionMgr.m_Lines.clear();
	debugDataMotionMgr.m_Quads.clear();
}

void CCmpUnitMotionManager::RenderForceVector(
	const MotionState& state,
	const CFixedVector2D& force,
	bool wasObstructed)
{
	if (force.IsZero())
		return;

	SOverlayLine line;

	// Calculate magnitude ratio for scaling (0-1 range)
	fixed pushMagnitude = force.Length();
	fixed maxExpectedPush = entity_pos_t::FromInt(15);
	float magnitudeRatio = std::min(1.0f, pushMagnitude.ToFloat() / maxExpectedPush.ToFloat());

	// Scale visualization based on force strength
	float lengthScale = 2.0f + (magnitudeRatio * 6.0f);
	float thicknessScale = 0.05f + (magnitudeRatio * 0.45f);

	CFixedVector2D pushDir = force;
	pushDir.Normalize();

	CVector3D startPos(
		state.pos.X.ToDouble(),
		state.cmpPosition->GetHeightFixed().ToDouble() + 15.1f,
		state.pos.Y.ToDouble()
	);

	CVector3D endPos(
		state.pos.X.ToDouble() + pushDir.X.ToDouble() * lengthScale,
		state.cmpPosition->GetHeightFixed().ToDouble() + 15.1f,
		state.pos.Y.ToDouble() + pushDir.Y.ToDouble() * lengthScale
	);

	line.PushCoords(startPos);
	line.PushCoords(endPos);
	line.m_Thickness = thicknessScale;

	// Color coding:
	// Red: obstructed movement
	// Orange: strong push (>70% of max)
	// Green: normal push
	// Yellow: minimal push
	if (wasObstructed)
		line.m_Color = CColor(1, 0, 0, 0.8f);
	else if (magnitudeRatio > 0.7f)
		line.m_Color = CColor(1, 0.5f, 0, 0.9f);
	else if (!force.IsZero())
		line.m_Color = CColor(0, 1, 0, 0.7f);
	else
		line.m_Color = CColor(1, 1, 0, 0.4f);

	debugDataMotionMgr.m_Lines.push_back(line);
}
#endif // DEBUG_RENDER

#undef DEBUG_STATS
#undef DEBUG_RENDER
