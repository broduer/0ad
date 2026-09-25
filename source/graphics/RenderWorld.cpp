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

#include "RenderWorld.h"

#include "graphics/Decal.h"
#include "graphics/Model.h"
#include "graphics/ModelAbstract.h"
#include "graphics/ObjectBase.h"
#include "graphics/ObjectEntry.h"
#include "graphics/ParticleManager.h"
#include "graphics/RenderableObject.h"
#include "graphics/Terrain.h"
#include "graphics/Unit.h"
#include "graphics/UnitAnimation.h"
#include "graphics/UnitManager.h"
#include "maths/Frustum.h"
#include "maths/MathUtil.h"
#include "maths/Matrix3D.h"
#include "maths/Vector2D.h"
#include "ps/ForEachInParallel.h"
#include "ps/Profile.h"
#include "renderer/Renderer.h"
#include "renderer/RenderingOptions.h"
#include "renderer/Scene.h"
#include "renderer/SceneRenderer.h"
#include "renderer/WaterManager.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "tools/atlas/GameInterface/GameLoop.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <mutex>
#include <numbers>
#include <utility>

namespace RenderWorld
{

namespace
{

constexpr std::uint32_t NO_PROJECTILE_ACTOR = 0;

// A turret chain deeper than this is a cycle.
constexpr int MAX_TURRET_DEPTH = 16;

} // anonymous namespace

const SUnitFeedback* SFeedback::Find(const entity_id_t entity) const
{
	const auto it = std::lower_bound(units.begin(), units.end(), entity,
		[](const SUnitFeedback& unit, const entity_id_t id) { return unit.entity < id; });
	return it != units.end() && it->entity == entity ? &*it : nullptr;
}

const std::wstring& SFeedback::GetProjectileActor(const SUnitFeedback& unit) const
{
	static const std::wstring none;
	if (!projectileActors || unit.projectileActor >= projectileActors->size())
		return none;
	return (*projectileActors)[unit.projectileActor];
}

void CPacketQueue::Push(CFramePacket&& packet)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Queued.push_back(std::move(packet));
}

void CPacketQueue::TakeAll(std::vector<CFramePacket>& out)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	for (CFramePacket& packet : m_Queued)
		out.push_back(std::move(packet));
	m_Queued.clear();
}

CFramePacket CPacketQueue::Recycle()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (m_Spare.empty())
		return {};
	CFramePacket packet{std::move(m_Spare.back())};
	m_Spare.pop_back();
	return packet;
}

void CPacketQueue::GiveBack(std::vector<CFramePacket>& spent)
{
	for (CFramePacket& packet : spent)
		packet.Clear();
	std::lock_guard<std::mutex> lock(m_Mutex);
	// A few spare packets cover the steady state.
	for (CFramePacket& packet : spent)
		if (m_Spare.size() < 4)
			m_Spare.push_back(std::move(packet));
	spent.clear();
}

struct CRenderWorld::SFeedbackPool
{
	std::mutex mutex;
	std::vector<std::unique_ptr<SFeedback>> spare;
};

CRenderWorld::CRenderWorld(CUnitManager& unitManager, CTerrain& terrain)
	: m_UnitManager{unitManager}, m_Terrain{terrain},
	m_ProjectileActors{std::make_shared<std::vector<std::wstring>>(1)},
	m_FeedbackPool{std::make_shared<SFeedbackPool>()}
{
	m_Feedback.store(std::make_shared<const SFeedback>());
}

CRenderWorld::~CRenderWorld()
{
	for (SUnit& unit : m_Units)
		DeleteActor(unit);
}

float CRenderWorld::GetExactGroundLevel(const float x, const float z) const
{
	return m_Terrain.GetExactGroundLevel(x, z);
}

CRenderWorld::SUnit* CRenderWorld::FindUnit(const entity_id_t entity)
{
	const auto it = m_UnitSlots.find(entity);
	return it == m_UnitSlots.end() ? nullptr : &m_Units[it->second];
}

const CRenderWorld::SUnit* CRenderWorld::FindUnit(const entity_id_t entity) const
{
	return const_cast<CRenderWorld*>(this)->FindUnit(entity);
}

std::vector<std::uint32_t>::iterator CRenderWorld::LowerBoundSlot(const entity_id_t entity)
{
	return std::lower_bound(m_SlotsByEntity.begin(), m_SlotsByEntity.end(), entity,
		[this](const std::uint32_t slot, const entity_id_t value) { return m_Units[slot].entity < value; });
}

const CRenderWorld::SPositionState* CRenderWorld::FindPosition(const entity_id_t entity) const
{
	const auto it = m_Positions.find(entity);
	return it == m_Positions.end() ? nullptr : &it->second;
}

void CRenderWorld::Apply(CFramePacket& packet)
{
	PROFILE3("render world apply");
	m_CullCandidatesValid = false;

	if (packet.waterLevel)
		m_WaterLevel = *packet.waterLevel;
	if (CRenderer::IsInitialised())
	{
		WaterManager& waterManager{g_Renderer.GetSceneRenderer().GetWaterManager()};
		if (packet.waterMapSize)
			waterManager.SetMapSize(*packet.waterMapSize);
		if (packet.waterChanged)
		{
			waterManager.RecomputeWaterData();
			waterManager.m_WaterHeight = m_WaterLevel;
		}
	}
	if (packet.waterChanged)
		m_Terrain.MakeRenderDataDirty(RENDERDATA_UPDATE_VERTICES);
	for (const CFramePacket::STerrainRegion& region : packet.dirtyTerrain)
		m_Terrain.MakeRenderDataDirty(region.i0, region.j0, region.i1, region.j1, RENDERDATA_UPDATE_VERTICES);
	if (packet.unitDebugOverlay)
		m_EnableDebugOverlays = *packet.unitDebugOverlay;
	if (packet.selectionDebugOverlay)
		m_Overlays.SetSelectionDebug(*packet.selectionDebugOverlay);
	if (packet.particlesUseSimTime)
		m_ParticlesUseSimTime = *packet.particlesUseSimTime;
	if (packet.los)
		m_Projectiles.SetLos(packet.los);

	if (packet.turnStarted)
	{
		for (const std::pair<entity_id_t, SPositionState*>& entry : m_Rotating)
		{
			SPositionState& state = *entry.second;
			if (state.sim.rotY == state.interpolatedRotY)
				continue;
			state.lastInterpolatedRotX = state.interpolatedRotX;
			state.lastInterpolatedRotZ = state.interpolatedRotZ;
		}
	}

	for (const VisualRecord& record : packet.visuals)
		std::visit([this](const auto& r) { ApplyRecord(r); }, record);

	for (const entity_id_t entity : packet.removedPositions)
	{
		if (SUnit* unit = FindUnit(entity))
			unit->position = nullptr;
		const auto it = m_Positions.find(entity);
		if (it == m_Positions.end())
			continue;
		if (it->second.rotating)
		{
			const auto rotating = std::find_if(m_Rotating.begin(), m_Rotating.end(),
				[entity](const std::pair<entity_id_t, SPositionState*>& entry) { return entry.first == entity; });
			*rotating = m_Rotating.back();
			m_Rotating.pop_back();
		}
		m_Positions.erase(it);
	}

	for (const SPosition& record : packet.positions)
		ApplyPosition(record);

	for (OverlayRecord& record : packet.overlays)
		m_Overlays.Apply(record);
	if (packet.territory)
		m_Overlays.ApplyTerritory(*packet.territory);
	for (SDebugLines& lines : packet.debugLines)
		m_Overlays.ApplyDebugLines(lines);
	for (const STerrainOverlayRecord& record : packet.terrainOverlays)
		m_Overlays.ApplyTerrainOverlay(record);
	for (const ProjectileRecord& record : packet.projectiles)
		m_Projectiles.Apply(record);
}

std::uint32_t CRenderWorld::InternProjectileActor(const std::wstring& name)
{
	if (name.empty())
		return NO_PROJECTILE_ACTOR;
	const auto it = m_ProjectileActorIndex.find(name);
	if (it != m_ProjectileActorIndex.end())
		return it->second;
	// Published tables are never changed; a new name gets a new table.
	std::shared_ptr<std::vector<std::wstring>> actors{std::make_shared<std::vector<std::wstring>>(*m_ProjectileActors)};
	const std::uint32_t index{static_cast<std::uint32_t>(actors->size())};
	actors->push_back(name);
	m_ProjectileActors = std::move(actors);
	m_ProjectileActorIndex.emplace(name, index);
	return index;
}

void CRenderWorld::BuildActor(SUnit& unit)
{
	const SVisual& visual = unit.visual;
	std::wstring actorName = visual.actorName;
	if (actorName.find(L".xml") == std::wstring::npos)
		actorName += L".xml";
	unit.actor = m_UnitManager.CreateUnit(actorName, unit.entity, visual.seed);
	unit.lastTransformFrame = -1;
	unit.flattened.clear();
	if (!unit.actor)
		return;

	CModelAbstract& model = unit.actor->GetModel();
	if (CModel* cmodel = model.ToCModel())
	{
		if (visual.modelFlags)
			cmodel->AddFlagsRec(visual.modelFlags);
		if (!visual.shadowsCast)
			cmodel->RemoveShadowsCast();
	}
	if (!visual.shadowsReceive)
	{
		if (model.ToCModel())
			model.ToCModel()->RemoveShadowsReceive();
		else if (model.ToCModelDecal())
			model.ToCModelDecal()->RemoveShadowsReceive();
	}

	const auto& properties = unit.actor->GetObject().m_Base->m_Properties;
	unit.actorAnchor = properties.m_AnchorType.empty() ? AnchorType::UNDEFINED : ParseAnchorType(properties.m_AnchorType);
	unit.actorFloating = properties.m_FloatOnWater;
	unit.projectileActor = InternProjectileActor(unit.actor->GetObject().m_ProjectileModelName);

	// The model is responsible for cleaning up the descriptor.
	if (visual.selectionShape)
		model.SetCustomSelectionShape(new CModelAbstract::CustomSelectionShape(*visual.selectionShape));

	// TODO: this should account for all possible props, animations, etc,
	// else we might accidentally cull the unit when it should be visible
	unit.boundsApprox = CBoundingSphere::FromSweptBox(model.GetWorldBoundsRec());
	UpdateSweptBounds(unit);

	// What the logic thread can know before the unit is first drawn.
	unit.out.bounds = model.GetWorldBounds();
	unit.out.selectionBox = model.GetSelectionBox();
	unit.out.position = model.GetTransform().GetTranslation();
	unit.out.hasLaunchPoint = false;
	unit.hasAmmo = model.ToCModel() && model.ToCModel()->FindFirstAmmoProp();
}

void CRenderWorld::ApplySelectionsAndAnimation(SUnit& unit)
{
	if (!unit.actor)
		return;
	const SVisual& visual = unit.visual;
	unit.actor->SetEntitySelection(visual.selections);
	CUnitAnimation* animation = unit.actor->GetAnimation();
	if (!animation)
		return;
	animation->SetAnimationState(visual.animation.name, visual.animation.once,
		visual.animation.speed, visual.animation.desync, visual.animation.soundGroup.c_str());
	// The exact synchronisation is lost, but at least the rate is right.
	if (visual.syncRepeat != 0.0f)
		animation->SetAnimationSyncRepeat(visual.syncRepeat);
	if (visual.syncOffset != 0.0f)
		animation->SetAnimationSyncOffset(visual.syncOffset);
}

void CRenderWorld::DeleteActor(SUnit& unit)
{
	if (unit.actor)
		m_UnitManager.DeleteUnit(unit.actor);
	unit.actor = nullptr;
	unit.flattened.clear();
	unit.lastTransformFrame = -1;
}

void CRenderWorld::ApplyRecord(const Visual::Create& record)
{
	std::uint32_t slot;
	if (const auto it = m_UnitSlots.find(record.entity); it != m_UnitSlots.end())
	{
		slot = it->second;
		DeleteActor(m_Units[slot]);
	}
	else
	{
		if (!m_FreeUnits.empty())
		{
			slot = m_FreeUnits.back();
			m_FreeUnits.pop_back();
		}
		else
		{
			slot = static_cast<std::uint32_t>(m_Units.size());
			m_Units.emplace_back();
		}
		m_SlotsByEntity.insert(LowerBoundSlot(record.entity), slot);
	}
	m_UnitSlots[record.entity] = slot;

	SUnit& unit = m_Units[slot];
	unit = SUnit{};
	unit.entity = record.entity;
	unit.visual = record.visual;
	unit.out.entity = record.entity;
	const auto position = m_Positions.find(record.entity);
	unit.position = position == m_Positions.end() ? nullptr : &position->second;
	BuildActor(unit);
}

void CRenderWorld::ApplyRecord(const Visual::Rebuild& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	DeleteActor(*unit);
	unit->visual = record.visual;
	BuildActor(*unit);
	if (!unit->actor)
		return;
	ApplySelectionsAndAnimation(*unit);
	unit->actor->GetModel().SetShadingColor(unit->visual.shading);
	unit->actor->GetModel().SetPlayerID(unit->visual.player);
}

void CRenderWorld::ApplyRecord(const Visual::ReloadAnimation& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual = record.visual;
	ApplySelectionsAndAnimation(*unit);
}

void CRenderWorld::ApplyRecord(const Visual::Destroy& record)
{
	const auto it = m_UnitSlots.find(record.entity);
	if (it == m_UnitSlots.end())
		return;
	m_SlotsByEntity.erase(LowerBoundSlot(record.entity));
	SUnit& unit = m_Units[it->second];
	DeleteActor(unit);
	unit = SUnit{};
	m_FreeUnits.push_back(it->second);
	m_UnitSlots.erase(it);
}

void CRenderWorld::ApplyRecord(const Visual::SetSelection& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual.selections[record.key] = record.selection;
	if (!unit->actor)
		return;
	const CObjectEntry* const object{&unit->actor->GetObject()};
	unit->actor->SetEntitySelection(record.key, record.selection);
	if (&unit->actor->GetObject() != object)
	{
		unit->hasAmmo = unit->actor->GetModel().ToCModel() && unit->actor->GetModel().ToCModel()->FindFirstAmmoProp();
		unit->projectileActor = InternProjectileActor(unit->actor->GetObject().m_ProjectileModelName);
		unit->lastTransformFrame = -1;
	}
	if (unit->actor->GetAnimation())
		unit->actor->GetAnimation()->ReloadAnimation();
}

void CRenderWorld::ApplyRecord(const Visual::SetAnimation& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual.animation = record.animation;
	unit->visual.syncRepeat = unit->visual.syncOffset = 0.0f;
	if (!unit->actor || !unit->actor->GetAnimation() || !unit->actor->GetID())
		return;
	unit->actor->GetAnimation()->SetAnimationState(record.animation.name, record.animation.once,
		record.animation.speed, record.animation.desync, record.animation.soundGroup.c_str());
}

void CRenderWorld::ApplyRecord(const Visual::SetSyncRepeat& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual.syncRepeat = record.time;
	if (unit->actor && unit->actor->GetAnimation())
		unit->actor->GetAnimation()->SetAnimationSyncRepeat(record.time);
}

void CRenderWorld::ApplyRecord(const Visual::SetSyncOffset& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual.syncOffset = record.time;
	if (unit->actor && unit->actor->GetAnimation())
		unit->actor->GetAnimation()->SetAnimationSyncOffset(record.time);
}

void CRenderWorld::ApplyRecord(const Visual::SetShading& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual.shading = record.color;
	if (unit->actor)
		unit->actor->GetModel().SetShadingColor(record.color);
}

void CRenderWorld::ApplyRecord(const Visual::SetPlayer& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (!unit)
		return;
	unit->visual.player = record.player;
	if (unit->actor)
		unit->actor->GetModel().SetPlayerID(record.player);
}

void CRenderWorld::ApplyRecord(const Visual::SetVariable& record)
{
	SUnit* unit = FindUnit(record.entity);
	if (unit && unit->actor)
		unit->actor->GetModel().SetEntityVariable(record.name, record.value);
}

void CRenderWorld::ApplyRecord(const Visual::SetVisibility& record)
{
	if (SUnit* unit = FindUnit(record.entity))
		unit->visibility = record.visibility;
}

void CRenderWorld::ApplyPosition(const SPosition& record)
{
	SPositionState& state = m_Positions[record.entity];
	SUnit* unit = FindUnit(record.entity);
	if (unit)
		unit->position = &state;
	const AnchorType actorAnchor{unit ? unit->actorAnchor : AnchorType::UNDEFINED};

	state.sim = record;
	if (record.turnStarted)
	{
		state.lastInterpolatedRotX = state.interpolatedRotX;
		state.lastInterpolatedRotZ = state.interpolatedRotZ;
		if (record.inWorld && record.turnMoved)
			UpdateXZRotation(state, actorAnchor);
	}
	if (record.snapRotY)
		state.interpolatedRotY = record.rotY;
	if (record.snapRotXZ && record.inWorld)
	{
		UpdateXZRotation(state, actorAnchor);
		state.lastInterpolatedRotX = state.interpolatedRotX;
		state.lastInterpolatedRotZ = state.interpolatedRotZ;
	}

	if (!state.rotating && state.sim.rotY != state.interpolatedRotY)
	{
		state.rotating = true;
		m_Rotating.emplace_back(record.entity, &state);
	}

	if (unit)
	{
		UpdateSweptBounds(*unit);
		unit->lastTransformFrame = -1;
	}
}

CVector3D CRenderWorld::InterpolatedPosition(const SPosition& sim, const bool last, const bool actorFloating) const
{
	const float x{last ? sim.lastX : sim.x};
	const float z{last ? sim.lastZ : sim.z};
	float baseY{0.0f};
	if (sim.relativeToGround)
	{
		baseY = m_Terrain.GetExactGroundLevel(x, z);
		if (sim.floating || actorFloating)
			baseY = std::max(baseY, m_WaterLevel - sim.floatDepth);
	}
	return {x, baseY + sim.y + (last ? sim.lastYDifference : 0.0f), z};
}

void CRenderWorld::UpdateSweptBounds(SUnit& unit)
{
	if (!unit.position || !unit.position->sim.inWorld)
	{
		unit.sweptBounds = CBoundingSphere(CVector3D(), unit.boundsApprox.GetRadius());
		return;
	}
	CVector3D pos0{InterpolatedPosition(unit.position->sim, true, unit.actorFloating)};
	CVector3D pos1{InterpolatedPosition(unit.position->sim, false, unit.actorFloating)};
	pos0.Y += GetConstructionProgressOffset(*unit.position, pos0);
	pos1.Y += GetConstructionProgressOffset(*unit.position, pos1);

	// The bounding sphere of the capsule formed by sweeping the approximate
	// bounds from pos0 to pos1.
	const CVector3D mid{(pos0 + pos1) * 0.5f + unit.boundsApprox.GetCenter()};
	const float radius{(pos1 - pos0).Length() * 0.5f + unit.boundsApprox.GetRadius()};
	unit.sweptBounds = CBoundingSphere(mid, radius);
}

void CRenderWorld::UpdateXZRotation(SPositionState& state, const AnchorType actorAnchor) const
{
	const SPosition& sim = state.sim;
	const AnchorType anchor{actorAnchor == AnchorType::UNDEFINED ? sim.anchorType : actorAnchor};
	if (anchor == AnchorType::UPRIGHT || sim.rotZ != 0.0f || sim.rotX != 0.0f)
	{
		// The visual rotations are the ones fixed by the simulation.
		state.interpolatedRotX = sim.rotX;
		state.interpolatedRotZ = sim.rotZ;
		return;
	}

	if (m_Terrain.GetVerticesPerSide() == 0)
		return;

	// TODO: average normal (average all the tiles?) for big units or for buildings?
	CVector3D normal{m_Terrain.CalcExactNormal(sim.x, sim.z)};

	// Rotate the normal so the positive x direction is in the direction of the unit.
	CVector2D projected{normal.X, normal.Z};
	projected.Rotate(state.interpolatedRotY);
	normal.X = projected.X;
	normal.Z = projected.Y;

	if (anchor == AnchorType::PITCH || anchor == AnchorType::PITCH_ROLL)
		state.interpolatedRotX = -std::atan2(normal.Z, normal.Y);
	if (anchor == AnchorType::ROLL || anchor == AnchorType::PITCH_ROLL)
		state.interpolatedRotZ = std::atan2(normal.X, normal.Y);
}

float CRenderWorld::GetConstructionProgressOffset(const SPositionState& state, const CVector3D& pos) const
{
	const SPosition& sim = state.sim;
	if (sim.constructionProgress == 0.0f)
		return 0.0f;

	const SUnit* unit = FindUnit(sim.entity);
	if (!unit || !unit->actor)
		return 0.0f;

	// Selection boxes give the model size, since the model could be offset.
	// TODO: this annoyingly shows decals, would be nice to hide them
	const CBoundingBoxOriented bounds{unit->actor->GetModel().GetSelectionBox()};
	if (bounds.IsEmpty())
		return 0.0f;

	float dy{2.0f * bounds.m_HalfSizes.Y};

	// A floating unit starts all the way under the terrain.
	if (sim.floating || unit->actorFloating)
		dy += std::max(0.0f, pos.Y - m_Terrain.GetExactGroundLevel(pos.X, pos.Z));

	return (sim.constructionProgress - 1.0f) * dy;
}

CMatrix3D CRenderWorld::InterpolatedTransform(const SPositionState& state, const int depth) const
{
	const SPosition& sim = state.sim;
	CMatrix3D identity;
	identity.SetIdentity();

	if (sim.turretParent != INVALID_ENTITY)
	{
		const SPositionState* parent{FindPosition(sim.turretParent)};
		if (!parent || !parent->sim.inWorld || depth >= MAX_TURRET_DEPTH)
			return identity;
		CMatrix3D own;
		own.SetYRotation(state.interpolatedRotY);
		own.Translate(-sim.turretPosition.X, sim.turretPosition.Y, -sim.turretPosition.Z);
		return InterpolatedTransform(*parent, depth + 1) * own;
	}
	if (!sim.inWorld)
		return identity;

	const float x{::Interpolate(sim.lastX, sim.x, m_FrameOffset)};
	const float z{::Interpolate(sim.lastZ, sim.z, m_FrameOffset)};

	const SUnit* unit{FindUnit(sim.entity)};
	const bool actorFloating{unit && unit->actorFloating};
	float baseY{0.0f};
	if (sim.relativeToGround)
	{
		baseY = m_Terrain.GetExactGroundLevel(x, z);
		if (sim.floating || actorFloating)
			baseY = std::max(baseY, m_WaterLevel - sim.floatDepth);
	}
	const float y{baseY + sim.y + ::Interpolate(-sim.lastYDifference, 0.0f, m_FrameOffset)};

	CMatrix3D m;
	// Linear interpolation is good enough for the tilt, which stays close to zero.
	m.SetXRotation(::Interpolate(state.lastInterpolatedRotX, state.interpolatedRotX, m_FrameOffset));
	m.RotateZ(::Interpolate(state.lastInterpolatedRotZ, state.interpolatedRotZ, m_FrameOffset));

	CVector3D pos{x, y, z};
	pos.Y += GetConstructionProgressOffset(state, pos);

	m.RotateY(state.interpolatedRotY + std::numbers::pi_v<float>);
	m.Translate(pos);
	return m;
}

bool CRenderWorld::IsInWorld(const entity_id_t entity) const
{
	const SPositionState* state{FindPosition(entity)};
	return state && state->sim.inWorld;
}

bool CRenderWorld::GetInterpolatedTransform(const entity_id_t entity, CMatrix3D& transform) const
{
	const SPositionState* state{FindPosition(entity)};
	if (!state)
		return false;
	transform = InterpolatedTransform(*state, 0);
	return true;
}

bool CRenderWorld::GetInterpolatedPosition2D(const entity_id_t entity, float& x, float& z, float& rotY) const
{
	const SPositionState* state{FindPosition(entity)};
	if (!state || !state->sim.inWorld)
		return false;
	x = ::Interpolate(state->sim.lastX, state->sim.x, m_FrameOffset);
	z = ::Interpolate(state->sim.lastZ, state->sim.z, m_FrameOffset);
	rotY = state->interpolatedRotY;
	return true;
}

CUnit* CRenderWorld::GetUnit(const entity_id_t entity) const
{
	const SUnit* unit{FindUnit(entity)};
	return unit ? unit->actor : nullptr;
}

void CRenderWorld::Interpolate(const float simFrameLength, const float frameOffset, const float realFrameLength,
	const bool paused, const CFrustum* cullFrustum, const bool culling)
{
	PROFILE3("render world interpolate");

	// Every frame positions its units afresh, paused or not, so a unit
	// rebuilt while paused is never drawn with its old model tree.
	++m_FrameNumber;
	m_CullCandidatesValid = false;
	if (paused)
		return;
	m_FrameOffset = frameOffset;

	for (size_t i = 0; i < m_Rotating.size();)
	{
		const entity_id_t entity{m_Rotating[i].first};
		SPositionState& state = *m_Rotating[i].second;
		const SPosition& sim = state.sim;
		if (sim.rotY == state.interpolatedRotY)
		{
			state.rotating = false;
			m_Rotating[i] = m_Rotating.back();
			m_Rotating.pop_back();
			continue;
		}
		++i;

		float delta{sim.rotY - state.interpolatedRotY};
		delta = std::fmod(delta + std::numbers::pi_v<float>, 2.0f * std::numbers::pi_v<float>); // range -2PI..2PI
		if (delta < 0.0f)
			delta += 2.0f * std::numbers::pi_v<float>; // range 0..2PI
		delta -= std::numbers::pi_v<float>; // range -PI..PI
		const float deltaClamped{Clamp(delta, -sim.rotYSpeed * simFrameLength, sim.rotYSpeed * simFrameLength)};
		// Keep the result close to the target angle, rather than n*2*PI away.
		state.interpolatedRotY = sim.rotY + deltaClamped - delta;

		if (sim.inWorld)
		{
			state.lastInterpolatedRotX = state.interpolatedRotX;
			state.lastInterpolatedRotZ = state.interpolatedRotZ;
			const SUnit* unit{FindUnit(entity)};
			UpdateXZRotation(state, unit ? unit->actorAnchor : AnchorType::UNDEFINED);
		}
	}

	// Position the units the camera sees right after their animation, while
	// their models are in cache and before the frame waits for a backbuffer;
	// the other cull groups position theirs at submission.
	AnimateAndPositionUnits(simFrameLength, cullFrustum, culling);

	m_Projectiles.Interpolate(simFrameLength);
	m_Overlays.Interpolate(simFrameLength, realFrameLength);
	if (CRenderer::IsInitialised())
	{
		g_Renderer.GetSceneRenderer().GetParticleManager().Interpolate(
			m_ParticlesUseSimTime ? simFrameLength : realFrameLength);
		g_Renderer.GetSceneRenderer().GetWaterManager().m_WaterTexTimer += simFrameLength;
	}

	m_DebugSpheres.clear();
	if (m_EnableDebugOverlays)
	{
		for (const SUnit& unit : m_Units)
		{
			if (!unit.actor || !unit.position || !unit.position->sim.inWorld)
				continue;
			SOverlaySphere sphere;
			sphere.m_Center = unit.sweptBounds.GetCenter();
			sphere.m_Radius = unit.sweptBounds.GetRadius();
			sphere.m_Color = unit.culled ? CColor(1.0f, 0.5f, 0.5f, 0.5f) : CColor(0.5f, 0.5f, 1.0f, 0.5f);
			m_DebugSpheres.push_back(sphere);
		}
	}
}

bool CRenderWorld::PositionUnit(SUnit& unit) const
{
	if (!unit.position)
		return false;

	CModelAbstract& unitModel = unit.actor->GetModel();
	unitModel.SetTransform(InterpolatedTransform(*unit.position, 0));
	if (CModel* model = unitModel.ToCModel())
		model->ValidatePosition();
	unit.worldBoundsRec = unitModel.GetWorldBoundsRec();
	unit.flattened.clear();
	SceneCollector::Flatten(&unitModel, unit.flattened);

	unit.out.bounds = unitModel.GetWorldBounds();
	unit.out.selectionBox = unitModel.GetSelectionBox();
	unit.out.position = unitModel.GetTransform().GetTranslation();
	unit.out.hasLaunchPoint = false;
	if (unit.hasAmmo)
	{
		if (CModel* model = unitModel.ToCModel())
		{
			if (CModelAbstract* ammo = model->FindFirstAmmoProp())
			{
				unit.out.launchPoint = ammo->GetTransform().GetTranslation();
				unit.out.hasLaunchPoint = true;
			}
		}
	}
	return true;
}

void CRenderWorld::AnimateAndPositionUnits(const float frameLength, const CFrustum* frustum, const bool culling)
{
	// TODO: we shouldn't update all the animations etc for units that are off-screen
	// (but need to be careful about e.g. sounds triggered by animations of off-screen
	// units)
	// A unit's animation touches only its own models and random sequence, so
	// the units advance on the workers in batches.
	m_ActionSoundsReached.assign(m_Units.size(), 0);
	Threading::ForEachInParallel(m_Units.size(), 8, [this, frameLength, frustum, culling](const size_t slot) {
		SUnit& unit = m_Units[slot];
		if (!unit.actor)
			return;
		const bool position{frustum && ShouldPosition(unit, *frustum, culling)};
		unit.actor->UpdateModel(frameLength);
		CUnitAnimation* animation{unit.actor->GetAnimation()};
		m_ActionSoundsReached[slot] = animation && animation->TakeActionSoundReached();
		if (position && PositionUnit(unit))
			unit.lastTransformFrame = m_FrameNumber;
	});

	// The sounds play in the order of the units' slots.
	for (size_t slot = 0; slot < m_Units.size(); ++slot)
	{
		if (!m_ActionSoundsReached[slot])
			continue;
		const CUnitAnimation& animation{*m_Units[slot].actor->GetAnimation()};
		m_FrameSounds.push_back({animation.GetEntity(), animation.GetActionSound()});
	}
	if (m_FrameSounds.empty())
		return;
	std::lock_guard<std::mutex> lock(m_SoundMutex);
	m_SoundEvents.insert(m_SoundEvents.end(), std::make_move_iterator(m_FrameSounds.begin()),
		std::make_move_iterator(m_FrameSounds.end()));
	m_FrameSounds.clear();
}

bool CRenderWorld::ShouldPosition(const SUnit& unit, const CFrustum& frustum, const bool culling) const
{
	if (!unit.actor || !unit.position || !unit.position->sim.inWorld || unit.lastTransformFrame == m_FrameNumber)
		return false;
	if (unit.visibility == LosVisibility::HIDDEN)
		return false;
	return !culling || frustum.IsSphereVisible(unit.sweptBounds.GetCenter(), unit.sweptBounds.GetRadius());
}

void CRenderWorld::CollectVisible(const CFrustum& frustum, const bool culling)
{
	if (!m_CullCandidatesValid)
	{
		m_CullCandidates.clear();
		for (size_t i = 0; i < m_Units.size(); ++i)
		{
			const SUnit& unit{m_Units[i]};
			if (unit.actor && unit.position && unit.position->sim.inWorld &&
				unit.visibility != LosVisibility::HIDDEN)
			{
				m_CullCandidates.push_back({unit.sweptBounds, static_cast<std::uint32_t>(i)});
			}
		}
		m_CullCandidatesValid = true;
	}
	m_VisibleSlots.clear();
	for (const SCullCandidate& candidate : m_CullCandidates)
		if (!culling || frustum.IsSphereVisible(candidate.bounds.GetCenter(), candidate.bounds.GetRadius()))
			m_VisibleSlots.push_back(candidate.slot);
}

void CRenderWorld::Submit(SceneCollector& collector, const CFrustum& frustum, const bool culling)
{
	PROFILE3("render world submit");

	CollectVisible(frustum, culling);

	// Each cull group positions the units no earlier one saw.
	m_UnitsToPosition.clear();
	for (const std::uint32_t slot : m_VisibleSlots)
		if (m_Units[slot].lastTransformFrame != m_FrameNumber)
			m_UnitsToPosition.push_back(slot);
	if (m_UnitsToPosition.size() >= 4 * 16)
	{
		Threading::ForEachInParallel(m_UnitsToPosition.size(), 16, [this](const size_t k) {
			SUnit& unit = m_Units[m_UnitsToPosition[k]];
			if (PositionUnit(unit))
				unit.lastTransformFrame = m_FrameNumber;
		});
	}

	if (m_EnableDebugOverlays)
		for (SUnit& unit : m_Units)
			unit.culled = true;

	for (const std::uint32_t slot : m_VisibleSlots)
	{
		SUnit& unit{m_Units[slot]};
		if (!g_AtlasGameLoop->running && !g_RenderingOptions.GetRenderActors() && unit.visual.actorOnly)
			continue;
		if (!g_AtlasGameLoop->running && unit.visual.visibleInAtlasOnly)
			continue;

		unit.culled = false;

		if (unit.lastTransformFrame != m_FrameNumber)
		{
			if (!PositionUnit(unit))
				continue;
			unit.lastTransformFrame = m_FrameNumber;
		}

		if (culling && !frustum.IsBoxVisible(unit.worldBoundsRec))
			continue;

		for (CModelAbstract* node : unit.flattened)
			collector.SubmitNode(node);
	}

	for (SOverlaySphere& sphere : m_DebugSpheres)
		collector.Submit(&sphere);

	m_Projectiles.Submit(collector, frustum, culling);
	m_Overlays.Submit(collector, frustum, culling);
}

void CRenderWorld::PublishFeedback()
{
	std::unique_ptr<SFeedback> spare;
	{
		std::lock_guard<std::mutex> lock(m_FeedbackPool->mutex);
		if (!m_FeedbackPool->spare.empty())
		{
			spare = std::move(m_FeedbackPool->spare.back());
			m_FeedbackPool->spare.pop_back();
		}
	}
	if (!spare)
		spare = std::make_unique<SFeedback>();
	// Whichever thread drops the last reference returns the snapshot.
	std::shared_ptr<SFeedback> feedback{spare.release(), [pool = m_FeedbackPool](SFeedback* released) {
		std::lock_guard<std::mutex> lock(pool->mutex);
		pool->spare.emplace_back(released);
	}};
	feedback->units.clear();
	feedback->units.reserve(m_SlotsByEntity.size());
	for (const std::uint32_t slot : m_SlotsByEntity)
	{
		SUnit& unit = m_Units[slot];
		if (!unit.actor)
			continue;
		unit.out.entity = unit.entity;
		unit.out.inWorld = unit.position && unit.position->sim.inWorld;
		unit.out.sweptBounds = unit.sweptBounds;
		unit.out.projectileActor = unit.projectileActor;
		feedback->units.push_back(unit.out);
	}
	feedback->projectileActors = m_ProjectileActors;
	m_Feedback.store(std::move(feedback));
}

std::shared_ptr<const SFeedback> CRenderWorld::GetFeedback() const
{
	return m_Feedback.load();
}

void CRenderWorld::TakeSoundEvents(std::vector<SSoundEvent>& out)
{
	std::lock_guard<std::mutex> lock(m_SoundMutex);
	out.swap(m_SoundEvents);
	m_SoundEvents.clear();
}

void CRenderWorld::ReloadActors(const std::wstring& actorName)
{
	m_CullCandidatesValid = false;
	const auto matches = [&actorName](const SUnit& unit) {
		return unit.entity != INVALID_ENTITY && (actorName.empty() ||
			unit.visual.actorName == actorName || unit.visual.actorName + L".xml" == actorName);
	};
	// Delete every affected unit before rebuilding any: rebuilding frees the outdated object
	// entries the others still point into.
	for (SUnit& unit : m_Units)
		if (matches(unit))
			DeleteActor(unit);
	for (SUnit& unit : m_Units)
	{
		if (!matches(unit))
			continue;
		BuildActor(unit);
		if (!unit.actor)
			continue;
		ApplySelectionsAndAnimation(unit);
		unit.actor->GetModel().SetShadingColor(unit.visual.shading);
		unit.actor->GetModel().SetPlayerID(unit.visual.player);
	}
}

} // namespace RenderWorld
