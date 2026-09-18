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
#include "simulation2/components/ICmpTerrain.h"
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
}

template<>
struct SerializeHelper<MotionState>
{
	template<typename S>
	void operator()(S& /*serialize*/, const char* /*name*/,
		Serialize::qualify<S, MotionState> /*value*/)
	{
		// Nothing to save: pushing pressure is kept by PhysicalBody, and everything else is rebuilt every turn.
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
	}
	for (EntityMap<MotionState>::iterator it = m_FormationControllers.begin(); it != m_FormationControllers.end(); ++it)
	{
		it->second.cmpPosition = static_cast<ICmpPosition*>(QueryInterface(GetSimContext(), it->first, IID_Position));
		it->second.cmpUnitMotion = static_cast<CCmpUnitMotion*>(static_cast<ICmpUnitMotion*>(QueryInterface(GetSimContext(), it->first, IID_UnitMotion)));
		it->second.cmpPhysicalBody = static_cast<ICmpPhysicalBody*>(QueryInterface(GetSimContext(), it->first, IID_PhysicalBody));
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
			compact.clearance = it->second.cmpUnitMotion->GetUnitClearance();
			compact.isMoving = it->second.isMoving;
			compact.ignore = it->second.ignore;
			compact.controlGroup = it->second.controlGroup;
			compact.weight = it->second.cmpPhysicalBody ?
				it->second.cmpPhysicalBody->GetWeight() :
				entity_pos_t::FromInt(10);

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
			// EntityMap::find is a direct index, so the target is found in constant time.
			// Only units the pre-move step collected (in world) are updated, as only those are applied below.
			// Not const: EntityMap's iterator compares and dereferences through non-const members.
			EntityMap<MotionState>::iterator it{ents.find(force.target)};
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
		const ICmpPathfinder& pathfinder{*cmpPathfinder.operator->()};
		for (auto* vec : assigned)
		{
			for (auto& it : *vec)
			{
				if (it->second.needUpdate)
				{
					it->second.cmpUnitMotion->Move(it->second, dt);
					ApplyPush(it->second, pathfinder);
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

void CCmpUnitMotionManager::ApplyPush(MotionState& state, const ICmpPathfinder& pathfinder)
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
	if (!pathfinder.CheckMovement(cmpUnitMotion.GetObstructionFilter(), state.pos.X, state.pos.Y,
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
