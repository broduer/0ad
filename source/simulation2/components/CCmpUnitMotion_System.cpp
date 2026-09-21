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
#include "simulation2/components/ICmpObstruction.h"
#include "simulation2/components/ICmpObstructionManager.h"
#include "simulation2/components/ICmpPathfinder.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpTerrain.h"
#include "simulation2/components/ICmpUnitMotion.h"
#include "simulation2/components/ICmpImpulseQueue.h"
#include "simulation2/components/ICmpAttack.h"

#include "simulation2/helpers/ChargeConfig.h"
#include "simulation2/helpers/UnitCollision.h"
#include "simulation2/helpers/PushConfig.h"
#include "simulation2/helpers/Grid.h"
#include "simulation2/helpers/ImpactEffects.h"
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
#include <unordered_set>
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
	, inertiaVelocity()
	, externalVelocity ()
	, angularVelocity (fixed::Zero())
	, impactAngularVelocity(fixed::Zero())
	, spinDecay(ChargeConfig::Get().spinDecay)
	, rollAngle(fixed::Zero())
	, rollVelocity(fixed::Zero())
	, maxRollAngle(fixed::Zero())
{
}

void CCmpUnitMotionManager::ClassInit(CComponentManager& componentManager)
{
	componentManager.SubscribeToMessageType(MT_Deserialized);
	componentManager.SubscribeToMessageType(MT_TerrainChanged);
	componentManager.SubscribeToMessageType(MT_TurnStart);
	componentManager.SubscribeToMessageType(MT_Update_Final);
	componentManager.SubscribeToMessageType(MT_Update_MotionUnit);
	componentManager.SubscribeToMessageType(MT_Update_MotionFormation);
	componentManager.SubscribeToMessageType(MT_ValueModification);
#if DEBUG_RENDER
	componentManager.SubscribeToMessageType(MT_RenderSubmit);
#endif
}

void CCmpUnitMotionManager::HandleMessage(const CMessage& msg, bool /*global*/)
{
	switch (msg.GetType())
	{
		case MT_TerrainChanged:
		{
			CmpPtr<ICmpTerrain> cmpTerrain(GetSystemEntity());
			if (cmpTerrain->GetVerticesPerSide() != m_MovingUnits.width())
				ResetSubdivisions();
			break;
		}
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
		case MT_ValueModification:
		{
			// A technology may have changed a unit's charge speed: read it again next turn.
			const CMessageValueModification& msgData = static_cast<const CMessageValueModification&>(msg);
			if (msgData.component != L"Attack")
				break;
			for (const entity_id_t ent : msgData.entities)
			{
				EntityMap<MotionState>::iterator it{m_Units.find(ent)};
				if (it != m_Units.end())
					it->second.chargeQueried = false;
			}
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
	ResetSubdivisions();
	m_ComputingMotion = false;
	// Load the settings at the start of each game.
	PushConfig::GetInstance().Reload();
	ChargeConfig::Reload();
}

template<>
struct SerializeHelper<MotionState>
{
	template<typename S>
	void operator()(S& serialize, const char* /*name*/,
		Serialize::qualify<S, MotionState> value)
	{
		// Serialize persistent inertia state
		serialize.NumberFixed_Unbounded("inertia_x", value.inertiaVelocity.X);
		serialize.NumberFixed_Unbounded("inertia_y", value.inertiaVelocity.Y);
		serialize.NumberFixed_Unbounded("external_inertia_x", value.externalInertiaVelocity.X);
		serialize.NumberFixed_Unbounded("external_inertia_y", value.externalInertiaVelocity.Y);

		// Turning, roll and collision state: all of it affects later turns, so it must survive saving/loading and rejoining.
		serialize.NumberFixed_Unbounded("angular_velocity", value.angularVelocity);
		serialize.NumberFixed_Unbounded("impact_angular_velocity", value.impactAngularVelocity);
		serialize.NumberFixed_Unbounded("push_spin", value.pushSpin);
		serialize.NumberFixed_Unbounded("roll_angle", value.rollAngle);
		serialize.NumberFixed_Unbounded("roll_velocity", value.rollVelocity);
		serialize.Bool("predicted_collision", value.predictedCollision);
		serialize.Bool("has_face_target", value.hasFaceTarget);
		serialize.NumberFixed_Unbounded("face_target_angle", value.faceTargetAngle);
	}
};

template<>
struct SerializeHelper<EntityMap<MotionState>>
{
	void operator()(ISerializer& serialize, const char* /*name*/,
		EntityMap<MotionState>& value)
	{
		// Serialize manually, we don't have a default-constructor for deserialization.
		Serializer(serialize, "size", static_cast<std::uint32_t>(value.size()));
		for (EntityMap<MotionState>::iterator it = value.begin(); it != value.end(); ++it)
		{
			Serializer(serialize, "ent id", it->first);
			Serializer(serialize, "state", it->second);
		}
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
			Serializer(deserialize, "state", state);
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
	ResetSubdivisions();
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
		it->second.cmpAttack = nullptr;
		it->second.chargeQueried = false;
	}
	for (EntityMap<MotionState>::iterator it = m_FormationControllers.begin(); it != m_FormationControllers.end(); ++it)
	{
		it->second.cmpPosition = static_cast<ICmpPosition*>(QueryInterface(GetSimContext(), it->first, IID_Position));
		it->second.cmpUnitMotion = static_cast<CCmpUnitMotion*>(static_cast<ICmpUnitMotion*>(QueryInterface(GetSimContext(), it->first, IID_UnitMotion)));
		it->second.cmpPhysicalBody = static_cast<ICmpPhysicalBody*>(QueryInterface(GetSimContext(), it->first, IID_PhysicalBody));
		it->second.cmpAttack = nullptr;
		it->second.chargeQueried = false;
	}
}

void CCmpUnitMotionManager::ResetSubdivisions()
{
	CmpPtr<ICmpTerrain> cmpTerrain(GetSystemEntity());
	if (!cmpTerrain)
		return;

	size_t size = cmpTerrain->GetMapSize();
	std::uint16_t gridSquareSize = static_cast<std::uint16_t>(size / PushConfig::PUSHING_GRID_SIZE + 1);
	m_MovingUnits.resize(gridSquareSize, gridSquareSize);
}

void CCmpUnitMotionManager::Register(CCmpUnitMotion* component, ICmpPhysicalBody* physicalBody, entity_id_t ent, bool formationController)
{
	MotionState state(static_cast<ICmpPosition*>(QueryInterface(GetSimContext(), ent, IID_Position)), component, physicalBody);

	// Set maxRollAngle
	if (physicalBody)
		state.maxRollAngle = physicalBody->GetMaxRollAngle();
	else
		state.maxRollAngle = fixed::Zero();

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
	std::unordered_set<std::vector<EntityMap<MotionState>::iterator>*> assigned;

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
			// Before the in-world check below: a unit leaving the world mustn't keep last turn's force.
			it->second.impulseForce = CFixedVector2D{};
			it->second.impulseVelocity = CFixedVector2D{};
			it->second.impulsePressure = 0;
			it->second.impulseSpin = fixed::Zero();

			if (!it->second.cmpPosition->IsInWorld())
			{
				it->second.needUpdate = false;
				continue;
			}

			// Update maxRollAngle from the component (it may have been loaded after registration)
			if (it->second.cmpPhysicalBody)
			{
				it->second.maxRollAngle = it->second.cmpPhysicalBody->GetMaxRollAngle();
				it->second.stagger = it->second.cmpPhysicalBody->GetStagger();
				it->second.staggerRecoveryRate = it->second.cmpUnitMotion->GetStaggerRecoveryRate();
			}
			it->second.spinDecay = it->second.cmpUnitMotion->GetSpinDecay();

			// Can this entity charge? Looked up once, then kept: the query costs as much as what it is used
			// for every turn, and the charge speed is a script call.
			if (!it->second.chargeQueried)
			{
				it->second.cmpAttack = static_cast<ICmpAttack*>(QueryInterface(GetSimContext(),
					it->first, IID_Attack));
				it->second.chargeMinSpeed = it->second.cmpAttack ?
					it->second.cmpAttack->GetChargeMinSpeed() : fixed::FromInt(-1);
				it->second.chargeQueried = true;
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
			compact.angle = it->second.angle;
			// The momentum the unit carries into this turn: where its mass is really going, rather than where it
			// faces. Not last turn's displacement, which also holds last turn's soft push: that push only eased
			// an overlap apart, and read as closing on the neighbour on the other side it would earn the unit a
			// contact impulse back, and a unit caught between two would be thrown back and forth.
			compact.velocity = it->second.cmpUnitMotion->CarriedVelocity(it->second, dt);
			compact.clearance = it->second.cmpUnitMotion->GetUnitClearance();
			compact.isMoving = it->second.isMoving;
			compact.ignore = it->second.ignore;
			compact.controlGroup = it->second.controlGroup;
			compact.weight = it->second.cmpPhysicalBody ?
				it->second.cmpPhysicalBody->GetWeight() :
				entity_pos_t::FromInt(10);
			// Without a PhysicalBody the unit keeps the plain clearance circle.
			compact.collisionShape = it->second.cmpPhysicalBody ?
				it->second.cmpPhysicalBody->GetCollisionShape() :
				UnitCollision::CollisionShape{compact.clearance, entity_pos_t::Zero(), false, false};

			// Charging: able to, and moving under its own power at least as fast as its charge needs. Then
			// what it runs into this turn is a hit, found by the pushing pass along with everything else it
			// touches. Its own speed on an order, not the displacement above: that includes pushes, and an
			// idle elephant shoved along at charge speed would charge its own herd.
			compact.charging = false;
			compact.minChargeSpeed = entity_pos_t::Zero();
			if (it->second.chargeMinSpeed >= fixed::Zero())
			{
				compact.minChargeSpeed = it->second.chargeMinSpeed;
				compact.charging = it->second.isMoving && it->second.speed >= compact.minChargeSpeed;
			}

			compactStates.push_back(compact);
			motionStates.push_back(&it->second);

			// Add to spatial grid
			int cellX = (it->second.pos.X / PushConfig::PUSHING_GRID_SIZE).ToInt_RoundToZero();
			int cellZ = (it->second.pos.Y / PushConfig::PUSHING_GRID_SIZE).ToInt_RoundToZero();
			cellX = std::max(0, std::min(cellX, m_MovingUnits.width() - 1));
			cellZ = std::max(0, std::min(cellZ, m_MovingUnits.height() - 1));

			std::vector<EntityMap<MotionState>::iterator>& subdiv = m_MovingUnits.get(cellX, cellZ);
			subdiv.emplace_back(it);
			assigned.emplace(&subdiv);
		}
	}

#if DEBUG_RENDER
	// Render all grid cells
	for (auto* vec : assigned)
	{
		if (!vec->empty())
			RenderGridCell(*vec, (*vec)[0]->second.cmpPosition);
	}
#endif

	// ========================================
	// COLLISION PUSHES
	// ========================================
	if (isUnitsPhase && IsPushingActivated() && !compactStates.empty())
	{
		PROFILE2("MotionMgr_GenerateCollisionForces");

		std::vector<CFixedVector2D> collisionForces;
		std::vector<fixed> collisionSpins;
		std::vector<CFixedVector2D> collisionVelocityChanges;
		std::vector<fixed> collisionHitSpins;
		std::vector<UnitCollision::ContactHit> collisionHits;
		std::vector<uint8_t> collisionPressures;
		UnitCollision::ComputePushes(compactStates, dt, m_PushGrid, m_PushGridFilledCells, collisionForces,
			collisionSpins, collisionVelocityChanges, collisionHitSpins, collisionHits, collisionPressures);

		if (impulseQueue && !collisionForces.empty())
		{
			std::vector<ImpulseRequest> requests;
			requests.reserve(compactStates.size());
			for (size_t i = 0; i < compactStates.size(); ++i)
				if ((!collisionForces[i].IsZero() || !collisionVelocityChanges[i].IsZero()) && !compactStates[i].ignore)
				{
					// A push that lands off a long body's centre also turns it; a circular body always
					// has a zero spin here, so nothing changes for the units that have one. A contact
					// impulse travels as a velocity change: applied at once like a hit, not built up,
					// with its spin as impact spin and, for a unit that can roll, the lean it gives.
					ImpulseRequest request{compactStates[i].entity, collisionForces[i], collisionPressures[i]};
					request.pushSpin = collisionSpins[i];
					request.velocityChange = collisionVelocityChanges[i];
					request.angularImpulse = collisionHitSpins[i];
					request.rollImpulse = ImpactEffects::RollFromVelocityChange(*motionStates[i], collisionVelocityChanges[i]);
					requests.push_back(request);
				}
			if (!requests.empty())
				impulseQueue->AddImpulses(requests);
		}

		// ========================================
		// CONTACT HITS
		// ========================================
		// A push from a charging unit can deal damage. The pushes above found every such contact; the
		// charger's Attack now decides what it means (stagger now, damage queued for the end of the turn).
		// A full blow is a hit at the charger's walk speed, or its run speed if charge.xml says so.
		{
			PROFILE2("MotionMgr_ContinuousCharge");
			for (const UnitCollision::ContactHit& hit : collisionHits)
			{
				EntityMap<MotionState>::iterator it{ents.find(hit.charger)};
				if (it == ents.end() || !it->second.cmpAttack)
					continue;
				fixed fullBlowSpeed{it->second.cmpUnitMotion->GetWalkSpeed()};
				if (ChargeConfig::Get().fullBlowAtRunSpeed)
					fullBlowSpeed = fullBlowSpeed.Multiply(it->second.cmpUnitMotion->GetRunMultiplier());
				it->second.cmpAttack->OnChargeContact(hit.target, hit.targetSpeedChange, hit.chargerSpeedChange,
					fullBlowSpeed);
			}
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
			// EntityMap::find is a direct index, so the target is found in constant time.
			// Only units the pre-move step collected (in world) are updated, as only those are applied below.
			// Not const: EntityMap's iterator compares and dereferences through non-const members.
			EntityMap<MotionState>::iterator it{ents.find(force.target)};
			if (it == ents.end() || !it->second.cmpPosition->IsInWorld())
				continue;

			MotionState& target{it->second};
			target.impulseForce += force.force;
			target.impulseVelocity += force.velocityChange;
			target.impulseSpin += force.pushSpin;
			target.impulsePressure = static_cast<uint8_t>(std::min<int>(PushConfig::MAX_PRESSURE,
				target.impulsePressure + force.pressureIncrease));

			// Spin (around the vertical axis) and roll (tilting sideways) from the impact.
			if (!force.angularImpulse.IsZero())
				ImpactEffects::AddSpinImpulse(target, force.angularImpulse);
			if (!force.rollImpulse.IsZero())
				ImpactEffects::AddRollImpulse(target, force.rollImpulse);
		}
	}

	// ========================================
	// EXTERNAL VELOCITY
	// ========================================
	// Turn the impulses into external velocity. PerformMove adds it to the unit's movement,
	// which is collision-checked there.
	if (IsPushingActivated())
	{
		PROFILE2("MotionMgr_ApplyForces");
		const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();

		for (MotionState* const state : motionStates)
		{
			// The thresholds are displacements: count a velocity change as the distance it moves the unit in
			// IMPACT_REFERENCE_TIME, so the same hit wakes the unit at any turn length.
			const CFixedVector2D impulse{state->impulseForce + state->impulseVelocity.Multiply(PushConfig::IMPACT_REFERENCE_TIME)};
			const bool hasPressure{state->impulsePressure > 0};
			// A velocity change is never dropped as too small: how small it is says nothing about whether it
			// matters. The threshold below ignores soft pushes too weak to matter, but a contact impulse is
			// sized to cancel exactly the speed at which two bodies are closing: a small one means two bodies
			// drifting into each other slowly, which is the very case the threshold would otherwise let
			// through. An attack's knockback skips it too: its size is the attack's Knockback over the unit's
			// weight, set by the template, and a pushing threshold has no business cancelling it. (Neither has
			// anything to do with damage, which is dealt elsewhere.)
			const bool hasVelocityChange{!state->impulseVelocity.IsZero()};
			const bool hasSignificantForce{hasVelocityChange || impulse.CompareLength(cfg.minimalPushing) > 0};

			// Apply pressure first
			if (hasPressure && state->cmpPhysicalBody)
				state->cmpPhysicalBody->AddPressure(state->impulsePressure);

			state->externalVelocity = CFixedVector2D{};
			// The spin is the angular effect of the push, so it is dropped and damped with it below.
			const fixed spinThisTurn{state->impulseSpin};
			state->impulseSpin = fixed::Zero();

			// Wake up units that received significant forces
			if (hasSignificantForce && !state->needUpdate)
			{
				const entity_pos_t MIN_FORCE_TO_WAKE{entity_pos_t::FromFraction(1, 10)};
				if (hasVelocityChange || impulse.Length() > MIN_FORCE_TO_WAKE)
				{
					state->needUpdate = true;
					state->pos = state->initialPos;

					// Flag its obstruction shape as moving for this turn. The movement check blocks a unit on
					// idle shapes but lets it pass moving ones, leaving pushing to keep moving units apart.
					// A pushed unit still flagged idle would be shoved into its neighbours as a wall and stop
					// them dead. Next turn PreMove sets the flag from the unit's own move request again.
					CmpPtr<ICmpObstruction> cmpObstruction(GetSimContext(), state->cmpUnitMotion->GetEntityId());
					if (cmpObstruction)
						cmpObstruction->SetMovingFlag(true);
					state->speed = state->cmpUnitMotion->GetCurrentSpeed();
					state->angle = state->initialAngle;
				}
			}

			if (!state->needUpdate || state->ignore || !state->cmpPhysicalBody || !hasSignificantForce ||
				dt <= fixed::Zero())
				continue;

			// Pressure damping
			const uint8_t currentPressure{state->cmpPhysicalBody->GetPressure()};
			const uint8_t dampingCap{std::min<uint8_t>(PushConfig::MAX_PUSH_DAMPING_PRESSURE, currentPressure)};
			const fixed damping{entity_pos_t::FromInt(PushConfig::MAX_PRESSURE - dampingCap) / PushConfig::MAX_PRESSURE};

			// Collision pushes are displacements for this turn (weight ratio and dt already applied in
			// UnitCollision::ComputePairPush), so they become a velocity over dt. Knockback and contact impulses
			// are velocity changes. PerformMove carries both on as external momentum, decaying by Drifting.
			const CFixedVector2D pushVelocity{state->impulseForce.Multiply(fixed::FromInt(1) / dt)};

			// Pressure damping applies to the soft push only. It stands for a crowd's give: a hemmed-in unit
			// has nowhere to go, so each further shove moves it less. A velocity change is either a hit or the
			// contact impulse that stops two bodies closing, and neither gets softer in a crowd. Damping the
			// contact impulse would cancel only part of the closing speed, and bodies in a cluster would keep
			// creeping into each other.
			// The velocity change first takes away the own momentum running against it (see
			// MomentumModel::AbsorbVelocityChange); only the rest joins the external momentum.
			const CFixedVector2D externalImpulse{MomentumModel::AbsorbVelocityChange(state->inertiaVelocity, state->impulseVelocity)};
			const CFixedVector2D externalVelocity{pushVelocity.Multiply(damping) + externalImpulse};

			// Ignore negligible velocities
			if (externalVelocity.CompareLength(entity_pos_t::FromFraction(1, 100)) >= 0)
				state->externalVelocity = externalVelocity;
			state->impulseSpin = spinThisTurn.Multiply(damping);
#if DEBUG_RENDER
			RenderForceVector(*state, state->externalVelocity, state->wasObstructed);
#endif
		}
	}

	// ========================================
	// MOVE
	// ========================================
	{
		PROFILE2("MotionMgr_MoveLoop");
		const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();
		for (auto* vec : assigned)
		{
			for (auto& it : *vec)
			{
				if (it->second.needUpdate)
				{
					// Always call Move - it will handle both active and inertia movement
					it->second.cmpUnitMotion->Move(it->second, dt);
				}

				// Decay pressure after moving
				if (IsPushingActivated() && it->second.cmpPhysicalBody && it->second.cmpPhysicalBody->GetPressure() > 0)
				{
					uint8_t currentPressure = it->second.cmpPhysicalBody->GetPressure();
					entity_pos_t decayed = entity_pos_t::FromInt(currentPressure).Multiply(cfg.pressureDecay);
					uint8_t newPressure = static_cast<uint8_t>(decayed.ToInt_RoundToZero());
					it->second.cmpPhysicalBody->SetPressure(newPressure);
				}
			}
		}
	}

	// ========================================
	// STAGGER
	// ========================================
	// Stagger wears off at each unit's own recovery rate.
	if (IsPushingActivated())
	{
		PROFILE2("MotionMgr_Stagger");
		for (auto* statePtr : motionStates)
		{
			if (statePtr->cmpPhysicalBody)
			{
				statePtr->cmpPhysicalBody->DecayStagger(statePtr->staggerRecoveryRate, dt);
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
	int size = 0;
	for (auto* vec : assigned)
		size += vec->size();
	double time = timer_Time() - start;
	if (size > 0)
		printf(">> Processed %d units in %f secs\n", size, time);
#endif

	// ========================================
	// CLEAR SPATIAL GRID
	// ========================================
	for (auto* vec : assigned)
		vec->clear();
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

void CCmpUnitMotionManager::RenderGridCell(
	const std::vector<EntityMap<MotionState>::iterator>& cell,
	ICmpPosition* pos)
{
	if (cell.empty())
		return;

	EntityMap<MotionState>::iterator firstIt = cell[0];
	int cellX = (firstIt->second.pos.X / PushConfig::PUSHING_GRID_SIZE).ToInt_RoundToZero();
	int cellZ = (firstIt->second.pos.Y / PushConfig::PUSHING_GRID_SIZE).ToInt_RoundToZero();

	SOverlayLine gridL;
	entity_pos_t gridMinX = entity_pos_t::FromInt(cellX * PushConfig::PUSHING_GRID_SIZE);
	entity_pos_t gridMinZ = entity_pos_t::FromInt(cellZ * PushConfig::PUSHING_GRID_SIZE);
	entity_pos_t gridMaxX = gridMinX + entity_pos_t::FromInt(PushConfig::PUSHING_GRID_SIZE);
	entity_pos_t gridMaxZ = gridMinZ + entity_pos_t::FromInt(PushConfig::PUSHING_GRID_SIZE);

	fixed height = pos->GetHeightFixed();

	auto addCorner = [&](entity_pos_t x, entity_pos_t z) {
		gridL.PushCoords(CVector3D(x.ToDouble(), height.ToDouble() + 2.f, z.ToDouble()));
	};

	addCorner(gridMinX, gridMinZ);
	addCorner(gridMaxX, gridMinZ);
	addCorner(gridMaxX, gridMaxZ);
	addCorner(gridMinX, gridMaxZ);
	addCorner(gridMinX, gridMinZ);

	gridL.m_Color = CColor(1, 1, 0, 1);
	debugDataMotionMgr.m_Lines.push_back(gridL);
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
