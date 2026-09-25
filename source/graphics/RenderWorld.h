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

#ifndef INCLUDED_RENDERWORLD
#define INCLUDED_RENDERWORLD

#include "graphics/FramePacket.h"
#include "graphics/GroundView.h"
#include "graphics/Overlay.h"
#include "graphics/RenderOverlays.h"
#include "graphics/RenderProjectiles.h"
#include "lib/code_annotation.h"
#include "maths/BoundingBoxAligned.h"
#include "maths/BoundingBoxOriented.h"
#include "maths/BoundingSphere.h"
#include "maths/Vector3D.h"
#include "ps/AtomicSharedPtr.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CFrustum;
class CMatrix3D;
class CModelAbstract;
class CTerrain;
class CUnit;
class CUnitManager;
class SceneCollector;

namespace RenderWorld
{

/**
 * What the logic thread may know about a unit's model, as of a frame the
 * render world drew.
 */
struct SUnitFeedback
{
	entity_id_t entity;
	bool inWorld;
	CBoundingSphere sweptBounds;
	CBoundingBoxAligned bounds;
	CBoundingBoxOriented selectionBox;
	CVector3D position;
	CVector3D launchPoint;
	bool hasLaunchPoint;
	// Index into SFeedback::projectileActors.
	std::uint32_t projectileActor;
};

/**
 * The render world's answer to the logic thread, replaced whole after every
 * frame and never changed once published.
 */
struct SFeedback
{
	// Sorted by entity.
	std::vector<SUnitFeedback> units;
	std::shared_ptr<const std::vector<std::wstring>> projectileActors;

	const SUnitFeedback* Find(entity_id_t entity) const;
	const std::wstring& GetProjectileActor(const SUnitFeedback& unit) const;
};

/**
 * An animation passed its action point, and the logic thread plays its sound.
 */
struct SSoundEvent
{
	entity_id_t entity;
	std::wstring soundGroup;
};

/**
 * Pass packets from the logic thread to the render world. Besides the published feedback, this is
 * the only state the two threads share.
 */
class CPacketQueue
{
public:
	void Push(CFramePacket&& packet);
	/**
	 * Move every queued packet into @p out, in order.
	 */
	void TakeAll(std::vector<CFramePacket>& out);
	/**
	 * Return a cleared packet to fill, reusing a spent one's storage if possible.
	 */
	CFramePacket Recycle();
	/**
	 * Keep some of the @p spent packets for Recycle, and empty @p spent.
	 */
	void GiveBack(std::vector<CFramePacket>& spent);

private:
	std::mutex m_Mutex;
	std::vector<CFramePacket> m_Queued;
	std::vector<CFramePacket> m_Spare;
};

/**
 * The render thread's world: every unit, its model tree and interpolation
 * state, built from the simulation's frame packets. Only the render thread
 * touches it, or the logic thread while the render thread is paused.
 */
class CRenderWorld final : public IGroundView
{
	NONCOPYABLE(CRenderWorld);
public:
	CRenderWorld(CUnitManager& unitManager, CTerrain& terrain);
	~CRenderWorld();

	float GetExactGroundLevel(float x, float z) const override;
	float GetWaterLevel() const override { return m_WaterLevel; }
	CTerrain* GetTerrain() const override { return &m_Terrain; }

	void Apply(CFramePacket& packet);

	/**
	 * Advance animations and interpolated rotations, and position the units @p cullFrustum sees.
	 * While paused, only invalidate the units' positions from the last frame.
	 */
	void Interpolate(float simFrameLength, float frameOffset, float realFrameLength, bool paused,
		const CFrustum* cullFrustum = nullptr, bool culling = true);

	void Submit(SceneCollector& collector, const CFrustum& frustum, bool culling);

	/**
	 * Publish what the last frame computed for the logic thread.
	 */
	void PublishFeedback();
	std::shared_ptr<const SFeedback> GetFeedback() const;

	/**
	 * Move the sound events since the last call to @p out, in order. Call on the logic thread.
	 */
	void TakeSoundEvents(std::vector<SSoundEvent>& out);

	/**
	 * Rebuild the units of an actor, or all units for an empty name.
	 */
	void ReloadActors(const std::wstring& actorName);

	void SetDebugOverlay(bool enabled) { m_EnableDebugOverlays = enabled; }

	float GetFrameOffset() const { return m_FrameOffset; }
	bool IsInWorld(entity_id_t entity) const;
	bool GetInterpolatedTransform(entity_id_t entity, CMatrix3D& transform) const;
	bool GetInterpolatedPosition2D(entity_id_t entity, float& x, float& z, float& rotY) const;
	CUnit* GetUnit(entity_id_t entity) const;

private:
	struct SPositionState
	{
		SPosition sim;
		float interpolatedRotX{0.0f}, interpolatedRotY{0.0f}, interpolatedRotZ{0.0f};
		float lastInterpolatedRotX{0.0f}, lastInterpolatedRotZ{0.0f};
		// Listed in m_Rotating: the interpolated rotation hasn't reached the simulation's.
		bool rotating{false};
	};

	struct SUnit
	{
		entity_id_t entity{INVALID_ENTITY};
		CUnit* actor{nullptr};
		SVisual visual;
		// Null until the entity's position arrives.
		SPositionState* position{nullptr};
		AnchorType actorAnchor{AnchorType::UNDEFINED};
		bool actorFloating{false};
		bool hasAmmo{false};
		std::uint32_t projectileActor{0};
		LosVisibility visibility{};

		// Worst-case bounds relative to the position, for any animation.
		CBoundingSphere boundsApprox;
		// Where the unit can be during the current turn.
		CBoundingSphere sweptBounds;

		int lastTransformFrame{-1};
		CBoundingBoxAligned worldBoundsRec;
		std::vector<CModelAbstract*> flattened;
		bool culled{true};
		SUnitFeedback out;
	};

	SUnit* FindUnit(entity_id_t entity);
	const SUnit* FindUnit(entity_id_t entity) const;
	std::vector<std::uint32_t>::iterator LowerBoundSlot(entity_id_t entity);
	const SPositionState* FindPosition(entity_id_t entity) const;

	void ApplyRecord(const Visual::Create& record);
	void ApplyRecord(const Visual::Rebuild& record);
	void ApplyRecord(const Visual::ReloadAnimation& record);
	void ApplyRecord(const Visual::Destroy& record);
	void ApplyRecord(const Visual::SetSelection& record);
	void ApplyRecord(const Visual::SetAnimation& record);
	void ApplyRecord(const Visual::SetSyncRepeat& record);
	void ApplyRecord(const Visual::SetSyncOffset& record);
	void ApplyRecord(const Visual::SetShading& record);
	void ApplyRecord(const Visual::SetPlayer& record);
	void ApplyRecord(const Visual::SetVariable& record);
	void ApplyRecord(const Visual::SetVisibility& record);
	void ApplyPosition(const SPosition& record);

	/**
	 * Build the unit's actor from its visual.
	 */
	void BuildActor(SUnit& unit);
	void ApplySelectionsAndAnimation(SUnit& unit);
	void DeleteActor(SUnit& unit);
	std::uint32_t InternProjectileActor(const std::wstring& name);

	void UpdateSweptBounds(SUnit& unit);
	void UpdateXZRotation(SPositionState& state, AnchorType actorAnchor) const;
	float GetConstructionProgressOffset(const SPositionState& state, const CVector3D& pos) const;
	CMatrix3D InterpolatedTransform(const SPositionState& state, int depth) const;
	CVector3D InterpolatedPosition(const SPosition& sim, bool last, bool actorFloating) const;

	bool PositionUnit(SUnit& unit) const;
	bool ShouldPosition(const SUnit& unit, const CFrustum& frustum, bool culling) const;
	/**
	 * Collect the slots of the units @p frustum sees into m_VisibleSlots.
	 */
	void CollectVisible(const CFrustum& frustum, bool culling);
	/**
	 * Advance every unit's animation and position the ones @p frustum sees, if given.
	 */
	void AnimateAndPositionUnits(float frameLength, const CFrustum* frustum, bool culling);

	CUnitManager& m_UnitManager;
	CTerrain& m_Terrain;
	float m_WaterLevel{0.0f};

	// Slots are reused last-freed first, so units keep the order the
	// simulation created them in.
	std::vector<SUnit> m_Units;
	std::vector<std::uint32_t> m_FreeUnits;
	std::unordered_map<entity_id_t, std::uint32_t> m_UnitSlots;
	// The used slots sorted by entity, the feedback's order.
	std::vector<std::uint32_t> m_SlotsByEntity;
	// Node-based, so units can keep pointers to their entity's position.
	std::unordered_map<entity_id_t, SPositionState> m_Positions;
	std::vector<std::pair<entity_id_t, SPositionState*>> m_Rotating;

	int m_FrameNumber{0};
	float m_FrameOffset{0.0f};
	// Per slot: whether the unit's animation reached an action sound.
	std::vector<std::uint8_t> m_ActionSoundsReached;
	std::vector<std::uint32_t> m_UnitsToPosition;

	// The bounds of the units that can be drawn at all, packed so each cull
	// group tests them without touching the units; rebuilt after any change.
	struct SCullCandidate
	{
		CBoundingSphere bounds;
		std::uint32_t slot;
	};
	std::vector<SCullCandidate> m_CullCandidates;
	bool m_CullCandidatesValid{false};
	std::vector<std::uint32_t> m_VisibleSlots;

	bool m_EnableDebugOverlays{false};
	std::vector<SOverlaySphere> m_DebugSpheres;

	COverlays m_Overlays{*this};
	CProjectiles m_Projectiles{m_UnitManager, *this};
	bool m_ParticlesUseSimTime{true};

	std::shared_ptr<std::vector<std::wstring>> m_ProjectileActors;
	std::unordered_map<std::wstring, std::uint32_t> m_ProjectileActorIndex;
	PS::AtomicSharedPtr<const SFeedback> m_Feedback;
	// Released snapshots to refill, so publishing allocates nothing in the
	// steady state.
	struct SFeedbackPool;
	std::shared_ptr<SFeedbackPool> m_FeedbackPool;

	std::vector<SSoundEvent> m_FrameSounds;
	std::mutex m_SoundMutex;
	std::vector<SSoundEvent> m_SoundEvents;
};

} // namespace RenderWorld

#endif // INCLUDED_RENDERWORLD
