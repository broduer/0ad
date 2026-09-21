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

#ifndef INCLUDED_CCMPUNITMOTION
#define INCLUDED_CCMPUNITMOTION

#include "graphics/Color.h"
#include "graphics/Overlay.h"
#include "lib/debug.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "maths/FixedVector3D.h"
#include "maths/MathUtil.h"
#include "ps/CLogger.h"
#include "ps/Profile.h"
#include "renderer/Scene.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/CCmpUnitMotionManager.h"
#include "simulation2/components/ICmpAttack.h"
#include "simulation2/components/ICmpObstruction.h"
#include "simulation2/components/ICmpObstructionManager.h"
#include "simulation2/components/ICmpPathfinder.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/components/ICmpUnitMotion.h"
#include "simulation2/components/ICmpUnitMotionManager.h"
#include "simulation2/components/ICmpValueModificationManager.h"
#include "simulation2/components/ICmpVisual.h"
#include "simulation2/components/ICmpPhysicalBody.h"
#include "simulation2/helpers/ChargeConfig.h"
#include "simulation2/helpers/HeadingControl.h"
#include "simulation2/helpers/ImpactEffects.h"
#include "simulation2/helpers/MomentumModel.h"
#include "simulation2/helpers/ObstructionResolution.h"
#include "simulation2/helpers/PathFollowing.h"
#include "simulation2/helpers/PathGoal.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/helpers/Render.h"
#include "simulation2/helpers/PushConfig.h"
#include "simulation2/helpers/MotionState.h"
#include "simulation2/serialization/SerializeTemplates.h"
#include "simulation2/serialization/SerializedPathfinder.h"
#include "simulation2/serialization/SerializedTypes.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Message.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// NB: this implementation of ICmpUnitMotion is very tightly coupled with UnitMotionManager.
// As such, both are compiled in the same TU.

// For debugging; units will start going straight to the target
// instead of calling the pathfinder
#define DISABLE_PATHFINDER 0

namespace
{
/**
 * Min/Max range to restrict short path queries to. (Larger ranges are (much) slower,
 * smaller ranges might miss some legitimate routes around large obstacles.)
 * NB: keep the max-range in sync with the vertex pathfinder "move the search space" heuristic.
 */
constexpr entity_pos_t SHORT_PATH_MIN_SEARCH_RANGE = entity_pos_t::FromInt(12 * Pathfinding::NAVCELL_SIZE_INT);
constexpr entity_pos_t SHORT_PATH_MAX_SEARCH_RANGE = entity_pos_t::FromInt(56 * Pathfinding::NAVCELL_SIZE_INT);
constexpr entity_pos_t SHORT_PATH_SEARCH_RANGE_INCREMENT = entity_pos_t::FromInt(4 * Pathfinding::NAVCELL_SIZE_INT);
constexpr std::uint8_t SHORT_PATH_SEARCH_RANGE_INCREASE_DELAY = 1;

/**
 * When using the short-pathfinder to rejoin a long-path waypoint, aim for a circle of this radius around the waypoint.
 */
constexpr entity_pos_t SHORT_PATH_LONG_WAYPOINT_RANGE = entity_pos_t::FromInt(4 * Pathfinding::NAVCELL_SIZE_INT);

/**
 * Minimum distance to goal for a long path request
 */
constexpr entity_pos_t LONG_PATH_MIN_DIST = entity_pos_t::FromInt(16 * Pathfinding::NAVCELL_SIZE_INT);

/**
 * If we are this close to our target entity/point, then think about heading
 * for it in a straight line instead of pathfinding.
 */
constexpr entity_pos_t DIRECT_PATH_RANGE = entity_pos_t::FromInt(24 * Pathfinding::NAVCELL_SIZE_INT);

/**
 * To avoid recomputing paths too often, have some leeway for target range checks
 * based on our distance to the target. Increase that incertainty by one navcell
 * for every this many tiles of distance.
 */
constexpr entity_pos_t TARGET_UNCERTAINTY_MULTIPLIER = entity_pos_t::FromInt(8 * Pathfinding::NAVCELL_SIZE_INT);

/**
 * When following a known imperfect path (i.e. a path that won't take us in range of our goal
 * we still recompute a new path every N turn to adapt to moving targets (for example, ships that must pickup
 * units may easily end up in this state, they still need to adjust to moving units).
 * This is rather arbitrary and mostly for simplicity & optimisation (a better recomputing algorithm
 * would not need this).
 */
constexpr std::uint8_t KNOWN_IMPERFECT_PATH_RESET_COUNTDOWN = 12;

/**
 * When we fail to move this many turns in a row, inform other components that the move will fail.
 * Experimentally, this number needs to be somewhat high or moving groups of units will lead to stuck units.
 * However, too high means units will look idle for a long time when they are failing to move.
 * TODO: if UnitMotion could send differentiated "unreachable" and "currently stuck" failing messages,
 * this could probably be lowered.
 */
constexpr std::uint8_t MAX_FAILED_MOVEMENTS = 35;

/**
 * When computing paths but failing to move, we want to occasionally alternate pathfinder systems
 * to avoid getting stuck (the short pathfinder can unstuck the long-range one and vice-versa, depending).
 */
constexpr std::uint8_t ALTERNATE_PATH_TYPE_DELAY = 3;
constexpr std::uint8_t ALTERNATE_PATH_TYPE_EVERY = 6;

/**
 * Units can occasionally get stuck near corners. The cause is a mismatch between CheckMovement and the short pathfinder.
 * The problem is the short pathfinder finds an impassable path when units are right on an obstruction edge.
 * Fixing this math mismatch is perhaps possible, but fixing it in UM is rather easy: just try backing up a bit
 * and that will probably un-stuck the unit. This is the 'failed movement' turn on which to try that.
 */
constexpr std::uint8_t BACKUP_HACK_DELAY = 10;

/**
 * Share of its own speed a unit has to keep for it to count as moving at all, so slow units moving normally
 * aren't taken for stuck (see MinProgressSpeed).
 * Braking can legitimately shave the speed down to a crawl, so 'didn't move exactly zero' is not a
 * usable test for progress: a unit inching along at a thousandth of a navcell per turn is stuck, but
 * it still passes every IsZero() check, keeps its animation running and never reaches the failed-movement
 * handling. Anything below this is treated as standing still.
 */
constexpr fixed MIN_PROGRESS_SHARE = fixed::FromFraction(1, 10);

/**
 * Likewise for rotation (radians/second): turning slower than this, a unit isn't pivoting, it's just settling.
 * (Turning in place is deliberate and must not be counted as a stall - a high-inertia unit spends
 * several turns doing it.)
 */
constexpr entity_angle_t MIN_ROTATION_SPEED = entity_angle_t::FromFraction(1, 20);

/**
 * After this many failed computations, start sending "VERY_OBSTRUCTED" messages instead.
 * Should probably be larger than ALTERNATE_PATH_TYPE_DELAY.
 */
constexpr std::uint8_t VERY_OBSTRUCTED_THRESHOLD = 10;

struct PathColorPalette
{
    CColor longPath;
    CColor shortPath;
};

constexpr PathColorPalette REGULAR_UNIT_PALETTE{{1, 1, 1, 1}, {1, 0, 0, 1}};
constexpr PathColorPalette FORMATION_CONTROLLER_PALETTE{{0, 0, 1, 1}, {0, 1, 0, 1}};
} // anonymous namespace

class CCmpUnitMotion final : public ICmpUnitMotion
{
	friend class CCmpUnitMotionManager;
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_Create);
		componentManager.SubscribeToMessageType(MT_Destroy);
		componentManager.SubscribeToMessageType(MT_PathResult);
		componentManager.SubscribeToMessageType(MT_OwnershipChanged);
		componentManager.SubscribeToMessageType(MT_ValueModification);
		componentManager.SubscribeToMessageType(MT_MovementObstructionChanged);
		componentManager.SubscribeToMessageType(MT_Deserialized);
	}

	DEFAULT_COMPONENT_ALLOCATOR(UnitMotion)

	bool m_DebugOverlayEnabled;
	std::vector<SOverlayLine> m_DebugOverlayLongPathLines;
	std::vector<SOverlayLine> m_DebugOverlayShortPathLines;

	// Template state:

	bool m_IsFormationController;

	fixed m_TemplateWalkSpeed, m_TemplateRunMultiplier;
	// The share of top speed given up at a right angle or sharper: 1 - Cornering, see the schema.
	fixed m_TemplateTurnBraking;
	pass_class_t m_PassClass;
	std::string m_PassClassName;

	// Dynamic state:

	entity_pos_t m_Clearance;

	// cached for efficiency
	fixed m_WalkSpeed, m_RunMultiplier;

	bool m_FacePointAfterMove;

	// Whether the unit blocks movement (& is blocked by movement blockers)
	// Cached from ICmpObstruction.
	bool m_BlockMovement = false;

	// Internal counter used when recovering from obstructed movement.
	// Most notably, increases the search range of the vertex pathfinder.
	// See HandleObstructedMove() for more details.
	std::uint8_t m_FailedMovements = 0;

	// If > 0, PathingUpdateNeeded returns false always.
	// This exists because the goal may be unreachable to the short/long pathfinder.
	// In such cases, we would compute inacceptable paths and PathingUpdateNeeded would trigger every turn,
	// which would be quite bad for performance.
	// To avoid that, when we know the new path is imperfect, treat it as OK and follow it anyways.
	// When reaching the end, we'll go through HandleObstructedMove and reset regardless.
	// To still recompute now and then (the target may be moving), this is a countdown decremented on each frame.
	std::uint8_t m_FollowKnownImperfectPathCountdown = 0;

	// Inertia strength multiplier for movement blending
	fixed m_TemplateInertia;

	// How much inertia carries over when not actively moving (0-1)
	fixed m_TemplateDrifting;

	// How much to boost pathfinding when drifting (0 = none, 1 = max)
	fixed m_TemplateDriftCorrection;

	// How fast the unit gains and loses turning speed, in radians per second squared
	fixed m_TemplateTurnAcceleration;

	// How fast the unit recovers from stagger
	fixed m_TemplateStaggerRecoveryRate;

	// How fast the spin from an impact decays, if this unit overrides the default in charge.xml
	std::optional<fixed> m_TemplateSpinDecay;

	struct Ticket {
		std::uint32_t m_Ticket = 0; // asynchronous request ID we're waiting for, or 0 if none
		enum Type {
			SHORT_PATH,
			LONG_PATH
		} m_Type = SHORT_PATH; // Pick some default value to avoid UB.

		void clear() { m_Ticket = 0; }
	} m_ExpectedPathTicket;

	struct MoveRequest {
		enum Type {
			NONE,
			POINT,
			ENTITY,
			OFFSET
		} m_Type = NONE;
		entity_id_t m_Entity = INVALID_ENTITY;
		CFixedVector2D m_Position;
		entity_pos_t m_MinRange, m_MaxRange;
		// Charging an attack target: don't brake on the final leg (see SetChargeMove).
		bool m_Charge = false;

		// For readability
		CFixedVector2D GetOffset() const { return m_Position; };

		MoveRequest() = default;
		MoveRequest(CFixedVector2D pos, entity_pos_t minRange, entity_pos_t maxRange) : m_Type(POINT), m_Position(pos), m_MinRange(minRange), m_MaxRange(maxRange) {};
		MoveRequest(entity_id_t target, entity_pos_t minRange, entity_pos_t maxRange) : m_Type(ENTITY), m_Entity(target), m_MinRange(minRange), m_MaxRange(maxRange) {};
		MoveRequest(entity_id_t target, CFixedVector2D offset) : m_Type(OFFSET), m_Entity(target), m_Position(offset) {};
	} m_MoveRequest;

	// If this is not INVALID_ENTITY, the unit is a formation member.
	entity_id_t m_FormationController = INVALID_ENTITY;

	// If the entity moves, it will do so at m_WalkSpeed * m_SpeedMultiplier.
	fixed m_SpeedMultiplier;
	// This caches the resulting speed from m_WalkSpeed * m_SpeedMultiplier for convenience.
	fixed m_Speed;

	// Mean speed over the last turn.
	fixed m_LastTurnSpeed;

	// The speed achieved at the end of the current turn.
	fixed m_CurrentSpeed;

	// Currently active paths (storing waypoints in reverse order).
	// The last item in each path is the point we're currently heading towards.
	WaypointPath m_LongPath;
	WaypointPath m_ShortPath;

	static std::string GetSchema()
	{
		return
			"<a:help>Provides the unit with the ability to move around the world by itself.</a:help>"
			"<a:example>"
				"<WalkSpeed>7.0</WalkSpeed>"
				"<PassabilityClass>default</PassabilityClass>"
			"</a:example>"
			"<element name='FormationController'>"
				"<data type='boolean'/>"
			"</element>"
			"<element name='WalkSpeed' a:help='Basic movement speed (in meters per second).'>"
				"<ref name='positiveDecimal'/>"
			"</element>"
			"<optional>"
				"<element name='RunMultiplier' a:help='How much faster the unit goes when running (as a multiple of walk speed).'>"
					"<ref name='positiveDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='Drifting' a:help='Fraction of its momentum a unit keeps every 0.2 seconds when not following a path, independent of turn length. Push momentum decays by this alone; the unit&apos;s own momentum keeps at least Inertia / (1 + Inertia). 1 = keep full speed.'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='Cornering' a:help='Share of its speed a unit keeps through a right angle or sharper turn. 1 = full speed (wide arcs), 0.7 = seven tenths, 0 = stop and turn in place. The code brakes by the rest.'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='Inertia' a:help='Inertia strength multiplier for movement blending. Higher values make units harder to turn/stop.'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='DriftCorrection' a:help='How tightly the unit holds its path: the fraction of its sideways drift it cancels each turn (at a cost in speed), how much it faces its waypoint rather than its direction of travel, and how close it has to get to a waypoint. 0 = none, 1 = full.'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<element name='TurnAcceleration' a:help='How fast the unit gains and loses turning speed, in radians per second squared. It starts turning at this rate, up to its TurnRate, and brakes at it to stop on its heading.'>"
				"<ref name='positiveDecimal'/>"
			"</element>"
			"<optional>"
				"<element name='StaggerRecoveryRate' a:help='Fraction of its stagger the unit recovers every 0.2 seconds, independent of turn length (0.05 = slow, 0.5 = fast).'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='SpinDecay' a:help='Fraction of the spin from an impact that the unit loses every 0.2 seconds, independent of turn length (0 to 1). Defaults to charge.xml.'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<element name='PassabilityClass' a:help='Identifies the terrain passability class (values are defined in special/pathfinder.xml).'>"
				"<text/>"
			"</element>"
			"<optional>"
				"<element name='DisablePushing'>"
					"<data type='boolean'/>"
				"</element>"
			"</optional>";
	}

	void Init(const CParamNode& paramNode) override
	{
		m_IsFormationController = paramNode.GetChild("FormationController").ToBool();

		m_FacePointAfterMove = true;

		m_WalkSpeed = m_TemplateWalkSpeed = m_Speed = paramNode.GetChild("WalkSpeed").ToFixed();
		m_SpeedMultiplier = fixed::FromInt(1);
		m_LastTurnSpeed = m_CurrentSpeed = fixed::Zero();

		m_RunMultiplier = m_TemplateRunMultiplier = fixed::FromInt(1);
		if (paramNode.GetChild("RunMultiplier").IsOk())
			m_RunMultiplier = m_TemplateRunMultiplier = paramNode.GetChild("RunMultiplier").ToFixed();

		// Cornering is the share of speed kept through a turn; what the steering wants is the share given up.
		if (paramNode.GetChild("Cornering").IsOk())
			m_TemplateTurnBraking = fixed::FromInt(1) - paramNode.GetChild("Cornering").ToFixed();
		else
			m_TemplateTurnBraking = fixed::FromFraction(2, 10);

		if (paramNode.GetChild("Drifting").IsOk())
			m_TemplateDrifting = paramNode.GetChild("Drifting").ToFixed();
		else
			m_TemplateDrifting = fixed::FromFraction(1, 10); // Minimal drifting but will only have effect if inertia isn't null

		if (paramNode.GetChild("Inertia").IsOk())
			m_TemplateInertia = paramNode.GetChild("Inertia").ToFixed();
		else
			m_TemplateInertia = fixed::Zero(); // No inertia by default

		if (paramNode.GetChild("DriftCorrection").IsOk())
			m_TemplateDriftCorrection = paramNode.GetChild("DriftCorrection").ToFixed();
		else
			m_TemplateDriftCorrection = fixed::Zero();

		m_TemplateTurnAcceleration = paramNode.GetChild("TurnAcceleration").ToFixed();

		if (paramNode.GetChild("StaggerRecoveryRate").IsOk())
			m_TemplateStaggerRecoveryRate = paramNode.GetChild("StaggerRecoveryRate").ToFixed();
		else
			m_TemplateStaggerRecoveryRate = fixed::FromFraction(1, 10);

		if (paramNode.GetChild("SpinDecay").IsOk())
			m_TemplateSpinDecay = paramNode.GetChild("SpinDecay").ToFixed();
		else
			m_TemplateSpinDecay.reset();

		m_PassClassName = paramNode.GetChild("PassabilityClass").ToString();
		SetPassabilityData(m_PassClassName);

		CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
		if (cmpObstruction)
			m_BlockMovement = cmpObstruction->GetBlockMovementFlag(true);

		SetParticipateInPushing(!paramNode.GetChild("DisablePushing").IsOk() || !paramNode.GetChild("DisablePushing").ToBool());

		m_DebugOverlayEnabled = false;
	}

	void Deinit() override
	{
	}

	template<typename S>
	void SerializeCommon(S& serialize)
	{
		// m_Clearance and m_PassClass are constructed from this.
		serialize.StringASCII("pass class", m_PassClassName, 0, 64);

		serialize.NumberU32_Unbounded("ticket", m_ExpectedPathTicket.m_Ticket);
		Serializer(serialize, "ticket type", m_ExpectedPathTicket.m_Type, Ticket::Type::LONG_PATH);

		serialize.NumberU8_Unbounded("failed movements", m_FailedMovements);
		serialize.NumberU8_Unbounded("followknownimperfectpath", m_FollowKnownImperfectPathCountdown);

		Serializer(serialize, "target type", m_MoveRequest.m_Type, MoveRequest::Type::OFFSET);
		serialize.NumberU32_Unbounded("target entity", m_MoveRequest.m_Entity);
		serialize.NumberFixed_Unbounded("target pos x", m_MoveRequest.m_Position.X);
		serialize.NumberFixed_Unbounded("target pos y", m_MoveRequest.m_Position.Y);
		serialize.NumberFixed_Unbounded("target min range", m_MoveRequest.m_MinRange);
		serialize.NumberFixed_Unbounded("target max range", m_MoveRequest.m_MaxRange);
		serialize.Bool("target charge", m_MoveRequest.m_Charge);

		serialize.NumberU32_Unbounded("formation controller", m_FormationController);

		serialize.NumberFixed_Unbounded("speed multiplier", m_SpeedMultiplier);

		serialize.NumberFixed_Unbounded("last turn speed", m_LastTurnSpeed);
		serialize.NumberFixed_Unbounded("current speed", m_CurrentSpeed);

		serialize.NumberFixed_Unbounded("turn_braking", m_TemplateTurnBraking);
		// Formation controllers take these from their members (see SetFormationMotionParameters); everyone else keeps
		// the template values, so there's nothing to save for them.
		if (m_IsFormationController)
		{
			serialize.NumberFixed_Unbounded("inertia", m_TemplateInertia);
			serialize.NumberFixed_Unbounded("drifting", m_TemplateDrifting);
			serialize.NumberFixed_Unbounded("drift correction", m_TemplateDriftCorrection);
		}

		serialize.Bool("facePointAfterMove", m_FacePointAfterMove);

		Serializer(serialize, "long path", m_LongPath.m_Waypoints);
		Serializer(serialize, "short path", m_ShortPath.m_Waypoints);
	}

	void Serialize(ISerializer& serialize) override
	{
		SerializeCommon(serialize);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);

		SerializeCommon(deserialize);

		SetPassabilityData(m_PassClassName);

		CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
		if (cmpObstruction)
			m_BlockMovement = cmpObstruction->GetBlockMovementFlag(false);
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_RenderSubmit:
		{
			PROFILE("UnitMotion::RenderSubmit");
			const CMessageRenderSubmit& msgData = static_cast<const CMessageRenderSubmit&> (msg);
			RenderSubmit(msgData.collector);
			break;
		}
		case MT_PathResult:
		{
			const CMessagePathResult& msgData = static_cast<const CMessagePathResult&> (msg);
			PathResult(msgData.ticket, msgData.path);
			break;
		}
		case MT_Create:
		{
			if (!ENTITY_IS_LOCAL(GetEntityId()))
			{
				CmpPtr<ICmpUnitMotionManager> cmpMotionMgr(GetSystemEntity());
				ICmpPhysicalBody* physicalBody = static_cast<ICmpPhysicalBody*>(
					QueryInterface(GetSimContext(), GetEntityId(), IID_PhysicalBody));
				cmpMotionMgr->Register(this, physicalBody, GetEntityId(), m_IsFormationController);
			}
			break;
		}
		case MT_Destroy:
		{
			if (!ENTITY_IS_LOCAL(GetEntityId()))
				CmpPtr<ICmpUnitMotionManager>(GetSystemEntity())->Unregister(GetEntityId());
			break;
		}
		case MT_MovementObstructionChanged:
		{
			CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
			if (cmpObstruction)
				m_BlockMovement = cmpObstruction->GetBlockMovementFlag(false);
			break;
		}
		case MT_ValueModification:
		{
			const CMessageValueModification& msgData = static_cast<const CMessageValueModification&> (msg);
			if (msgData.component != L"UnitMotion")
				break;
			[[fallthrough]];
		}
		case MT_OwnershipChanged:
		{
			OnValueModification();
			break;
		}
		case MT_Deserialized:
		{
			OnValueModification();
			break;
		}
		}
	}

	void UpdateMessageSubscriptions()
	{
		bool needRender = m_DebugOverlayEnabled;
		GetSimContext().GetComponentManager().DynamicSubscriptionNonsync(MT_RenderSubmit, this, needRender);
	}

	bool IsMoveRequested() const override
	{
		return m_MoveRequest.m_Type != MoveRequest::NONE;
	}

	bool IsMovingAsFormation() const override
	{
		return IsFormationMember() && m_MoveRequest.m_Type == MoveRequest::OFFSET;
	}

	fixed GetSpeedMultiplier() const override
	{
		return m_SpeedMultiplier;
	}

	void SetSpeedMultiplier(fixed multiplier) override
	{
		m_SpeedMultiplier = std::min(multiplier, m_RunMultiplier);
		m_Speed = m_SpeedMultiplier.Multiply(GetWalkSpeed());
	}

	fixed GetSpeed() const override
	{
		return m_Speed;
	}

	fixed GetWalkSpeed() const override
	{
		return m_WalkSpeed;
	}

	fixed GetRunMultiplier() const override
	{
		return m_RunMultiplier;
	}

	CFixedVector2D EstimateFuturePosition(const fixed dt) const override
	{
		CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
		if (!cmpPosition || !cmpPosition->IsInWorld())
			return CFixedVector2D();

		// TODO: formation members should perhaps try to use the controller's position.

		CFixedVector2D pos = cmpPosition->GetPosition2D();
		entity_angle_t angle = cmpPosition->GetRotation().Y;
		fixed speed = m_CurrentSpeed;
		// Copy the path so we don't change it.
		WaypointPath shortPath = m_ShortPath;
		WaypointPath longPath = m_LongPath;

		MotionState tempState(nullptr, const_cast<CCmpUnitMotion*>(this), nullptr);
		tempState.pos = pos;
		tempState.speed = speed;
		tempState.angle = angle;
		tempState.inertiaVelocity = CFixedVector2D();
		tempState.externalVelocity = CFixedVector2D();
		tempState.angularVelocity = fixed::Zero();

		PerformMove(dt, cmpPosition->GetTurnRate(), shortPath, longPath, tempState, 0);
		return tempState.pos;
	}

	pass_class_t GetPassabilityClass() const override
	{
		return m_PassClass;
	}

	std::string GetPassabilityClassName() const override
	{
		return m_PassClassName;
	}

	void SetPassabilityClassName(const std::string& passClassName) override
	{
		if (!m_IsFormationController)
		{
			LOGWARNING("Only formation controllers can change their passability class");
			return;
		}
		SetPassabilityData(passClassName);
	}

	fixed GetInertia() const override
	{
		return m_TemplateInertia;
	}

	fixed GetDrifting() const override
	{
		return m_TemplateDrifting;
	}

	void SetFormationMotionParameters(const fixed inertia, const fixed drifting) override
	{
		if (!m_IsFormationController)
		{
			LOGWARNING("Only formation controllers can change their motion parameters");
			return;
		}
		// Momentum comes from the members, so the controller never outruns the heaviest of them.
		m_TemplateInertia = inertia;
		m_TemplateDrifting = drifting;

		// Steering doesn't: the controller is a marker the members follow, so it holds its path as precisely as it can
		// and stops to turn rather than sweeping wide arcs the members then have to chase (which would have them circling
		// in forests). What the formation looks like comes from the members' own steering.
		m_TemplateTurnBraking = fixed::FromInt(1);
		m_TemplateDriftCorrection = fixed::FromInt(1);
	}

	fixed GetCurrentSpeed() const override
	{
		return m_CurrentSpeed;
	}

	void SetCurrentSpeed(const fixed& speed) override
	{
		m_CurrentSpeed = speed;

		if (speed == fixed::Zero())
			m_LastTurnSpeed = fixed::Zero();
		else
			m_LastTurnSpeed = speed;
	}

	void SetFacePointAfterMove(bool facePointAfterMove) override
	{
		m_FacePointAfterMove = facePointAfterMove;
	}

	bool GetFacePointAfterMove() const override
	{
		return m_FacePointAfterMove;
	}

	void SetDebugOverlay(bool enabled) override
	{
		m_DebugOverlayEnabled = enabled;
		UpdateMessageSubscriptions();
	}

	bool MoveToPointRange(entity_pos_t x, entity_pos_t z, entity_pos_t minRange, entity_pos_t maxRange) override
	{
		return MoveTo(MoveRequest(CFixedVector2D(x, z), minRange, maxRange));
	}

	bool MoveToTargetRange(entity_id_t target, entity_pos_t minRange, entity_pos_t maxRange) override
	{
		return MoveTo(MoveRequest(target, minRange, maxRange));
	}

	void SetChargeMove() override
	{
		if (m_MoveRequest.m_Type != MoveRequest::ENTITY)
			return;

		CmpPtr<ICmpAttack> cmpAttack(GetEntityHandle());
		if (cmpAttack && cmpAttack->GetChargeMinSpeed() >= fixed::Zero())
			m_MoveRequest.m_Charge = true;
	}

	void MoveToFormationOffset(entity_id_t controller, entity_pos_t x, entity_pos_t z) override
	{
		// Pass the controller to the move request anyways.
		MoveTo(MoveRequest(controller, CFixedVector2D(x, z)));
	}

	void SetMemberOfFormation(entity_id_t controller) override
	{
		m_FormationController = controller;
	}

	std::optional<CFixedVector2D> GetFormationOffset() const override
	{
		if (m_MoveRequest.m_Type != MoveRequest::OFFSET)
			return std::nullopt;

		return m_MoveRequest.m_Position;
	}

	bool IsTargetRangeReachable(entity_id_t target, entity_pos_t minRange, entity_pos_t maxRange) override;

	void FaceTowardsPoint(entity_pos_t x, entity_pos_t z) override;

	/**
	 * Clears the current MoveRequest - the unit will stop and no longer try and move.
	 * This should never be called from UnitMotion, since MoveToX orders are given
	 * by other components - these components should also decide when to stop.
	 */
	void StopMoving() override
	{
		// Face the destination only when we actually arrived. When a move is interrupted, e.g. by a new order that stops
		// the current move first, facing the abandoned destination would snap the unit toward it right before it starts
		// turning to its new heading. Nor when standing on it: the unit comes to rest anywhere within the distance it
		// may stop at, and what small remainder is left of the way to the point says nothing about where to
		// face, while the heading it arrived with does.
		CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
		std::optional<CFixedVector2D> facePoint;
		if (m_FacePointAfterMove && cmpPosition && cmpPosition->IsInWorld() && PossiblyAtDestination())
		{
			CFixedVector2D targetPos;
			if (ComputeTargetPosition(targetPos) &&
				(targetPos - cmpPosition->GetPosition2D()).CompareLength(Pathfinding::NAVCELL_SIZE + StoppingMargin()) > 0)
				facePoint = targetPos;
		}

		m_MoveRequest = MoveRequest();
		m_ExpectedPathTicket.clear();
		m_LongPath.m_Waypoints.clear();
		m_ShortPath.m_Waypoints.clear();

		// Only now that the move request is cleared: facing requests are ignored while moving.
		if (facePoint)
			FaceTowardsPointFromPos(cmpPosition->GetPosition2D(), facePoint->X, facePoint->Y);
	}

	entity_pos_t GetUnitClearance() const override
	{
		return m_Clearance;
	}

	/**
	 * Fraction of our own (pathfinding) inertia kept every IMPACT_REFERENCE_TIME when not following a path, e.g. after
	 * stopping. Inertia / (1 + Inertia) coasts us about speed · IMPACT_REFERENCE_TIME · (1 + Inertia), the same
	 * stopping distance the braking code assumes; Drifting can only lengthen that.
	 * Arrivals are braked first, so they coast little; charges skip that braking (see SetChargeMove) and carry through
	 * contact. Moves interrupted at speed (stop orders, new orders) coast too.
	 */
	fixed GetInertiaKeep() const
	{
		const fixed inertiaKeep{m_TemplateInertia / (fixed::FromInt(1) + m_TemplateInertia)};
		return std::max(m_TemplateDrifting, inertiaKeep);
	}

	/**
	 * The speed left after coasting for dt with no order: the GetInertiaKeep() share, but never more than braking at
	 * the constant deceleration arrivals use (see PathFollowing::PredictiveBrakeFactor), so the glide ends.
	 */
	fixed DecayCoastingSpeed(const fixed speed, const fixed dt) const
	{
		const fixed kept{speed.Multiply(PushConfig::DecayFactor(GetInertiaKeep(), dt))};
		const fixed stoppingTime{PushConfig::IMPACT_REFERENCE_TIME.Multiply(fixed::FromInt(1) + m_TemplateInertia) * 2};
		if (stoppingTime <= fixed::Zero())
			return kept;
		const fixed braked{std::max(speed - m_WalkSpeed.Multiply(dt) / stoppingTime, fixed::Zero())};
		return std::min(kept, braked);
	}

	/**
	 * The velocity a unit carries into this turn: its own momentum, and the push momentum it keeps (see Drifting).
	 */
	CFixedVector2D CarriedVelocity(const MotionState& state, const fixed dt) const
	{
		return state.inertiaVelocity + state.externalInertiaVelocity.Multiply(PushConfig::DecayFactor(m_TemplateDrifting, dt));
	}

	fixed GetStaggerRecoveryRate() const
	{
		return m_TemplateStaggerRecoveryRate;
	}

	fixed GetSpinDecay() const
	{
		return m_TemplateSpinDecay.value_or(ChargeConfig::Get().spinDecay);
	}

private:
	/**
	 * The slowest a unit can go and still count as moving: MIN_PROGRESS_SHARE of its own speed.
	 */
	fixed MinProgressSpeed() const
	{
		return m_Speed.Multiply(MIN_PROGRESS_SHARE);
	}

	/**
	 * How far from the end of its path a unit brakes to a stop: a navcell inside the distance at which it is
	 * in range of a point or a target, which is its clearance (see PossiblyAtDestination), so it comes to rest in
	 * range rather than creeping up on the edge of it. A formation slot is reached by the centre.
	 */
	fixed StoppingMargin() const
	{
		return m_MoveRequest.m_Type == MoveRequest::OFFSET ? fixed::Zero() :
			std::max(m_Clearance - Pathfinding::NAVCELL_SIZE, fixed::Zero());
	}

	bool IsFormationMember() const
	{
		return m_FormationController != INVALID_ENTITY;
	}

	bool IsFormationControllerMoving() const
	{
		CmpPtr<ICmpUnitMotion> cmpControllerMotion(GetSimContext(), m_FormationController);
		return cmpControllerMotion && cmpControllerMotion->IsMoveRequested();
	}

	entity_id_t GetGroup() const
	{
		return IsFormationMember() ? m_FormationController : GetEntityId();
	}

	void SetParticipateInPushing(bool pushing)
	{
		CmpPtr<ICmpUnitMotionManager> cmpUnitMotionManager(GetSystemEntity());
		CmpPtr<ICmpPhysicalBody> cmpPhysicalBody(GetEntityHandle());
		if (cmpPhysicalBody)
			cmpPhysicalBody->SetEnabled(pushing && cmpUnitMotionManager->IsPushingActivated());
	}

	void SetPassabilityData(const std::string& passClassName)
	{
		m_PassClassName = passClassName;
		CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
		if (cmpPathfinder)
		{
			m_PassClass = cmpPathfinder->GetPassabilityClass(passClassName);
			m_Clearance = cmpPathfinder->GetClearance(m_PassClass);

			CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
			if (cmpObstruction)
				cmpObstruction->SetUnitClearance(m_Clearance);
		}
	}

	/**
	 * Warns other components that our current movement will likely fail (e.g. we won't be able to reach our target)
	 * This should only be called before the actual movement in a given turn, or units might both move and try to do things
	 * on the same turn, leading to gliding units.
	 */
	void MoveFailed()
	{
		// Don't notify if we are a formation member in a moving formation - we can occasionally be stuck for a long time
		// if our current offset is unreachable, but we don't want to end up stuck.
		// (If the formation controller has stopped moving however, we can safely message).
		if (IsFormationMember() && IsFormationControllerMoving())
			return;

		CMessageMotionUpdate msg(CMessageMotionUpdate::LIKELY_FAILURE);
		GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
	}

	/**
	 * Warns other components that our current movement is likely over (i.e. we probably reached our destination)
	 * This should only be called before the actual movement in a given turn, or units might both move and try to do things
	 * on the same turn, leading to gliding units.
	 */
	void MoveSucceeded()
	{
		// Don't notify if we are a formation member in a moving formation - we can occasionally be stuck for a long time
		// if our current offset is unreachable, but we don't want to end up stuck.
		// (If the formation controller has stopped moving however, we can safely message).
		if (IsFormationMember() && IsFormationControllerMoving())
			return;

		CMessageMotionUpdate msg(CMessageMotionUpdate::LIKELY_SUCCESS);
		GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
	}

	/**
	 * Warns other components that our current movement was obstructed (i.e. we failed to move this turn).
	 * This should only be called before the actual movement in a given turn, or units might both move and try to do things
	 * on the same turn, leading to gliding units.
	 */
	void MoveObstructed()
	{
		// Don't notify if we are a formation member in a moving formation - we can occasionally be stuck for a long time
		// if our current offset is unreachable, but we don't want to end up stuck.
		// (If the formation controller has stopped moving however, we can safely message).
		if (IsFormationMember() && IsFormationControllerMoving())
			return;

		CMessageMotionUpdate msg(m_FailedMovements >= VERY_OBSTRUCTED_THRESHOLD ?
			CMessageMotionUpdate::VERY_OBSTRUCTED : CMessageMotionUpdate::OBSTRUCTED);
		GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
	}

	/**
	 * Increment the number of failed movements and notify other components if required.
	 * @returns true if the failure was notified, false otherwise.
	 */
	bool IncrementFailedMovementsAndMaybeNotify()
	{
		m_FailedMovements++;
		if (m_FailedMovements >= MAX_FAILED_MOVEMENTS)
		{
			MoveFailed();
			m_FailedMovements = 0;
			return true;
		}
		return false;
	}

	/**
	 * If path would take us farther away from the goal than pos currently is, return false, else return true.
	 */
	bool RejectFartherPaths(const PathGoal& goal, const WaypointPath& path, const CFixedVector2D& pos) const;

	bool ShouldAlternatePathfinder() const
	{
		return (m_FailedMovements == ALTERNATE_PATH_TYPE_DELAY) || ((MAX_FAILED_MOVEMENTS - ALTERNATE_PATH_TYPE_DELAY) % ALTERNATE_PATH_TYPE_EVERY == 0);
	}

	bool InShortPathRange(const PathGoal& goal, const CFixedVector2D& pos) const
	{
		return goal.DistanceToPoint(pos) < LONG_PATH_MIN_DIST;
	}

	entity_pos_t ShortPathSearchRange() const
	{
		std::uint8_t multiple = m_FailedMovements < SHORT_PATH_SEARCH_RANGE_INCREASE_DELAY ? 0 :
			m_FailedMovements - SHORT_PATH_SEARCH_RANGE_INCREASE_DELAY;
		fixed searchRange = SHORT_PATH_MIN_SEARCH_RANGE + SHORT_PATH_SEARCH_RANGE_INCREMENT * multiple;
		if (searchRange > SHORT_PATH_MAX_SEARCH_RANGE)
			searchRange = SHORT_PATH_MAX_SEARCH_RANGE;
		return searchRange;
	}

	/**
	 * Handle the result of an asynchronous path query.
	 */
	void PathResult(std::uint32_t ticket, const WaypointPath& path);

	void OnValueModification()
	{
		CmpPtr<ICmpValueModificationManager> cmpValueModificationManager(GetSystemEntity());
		if (!cmpValueModificationManager)
			return;

		m_WalkSpeed = cmpValueModificationManager->ApplyModifications(L"UnitMotion/WalkSpeed", m_TemplateWalkSpeed, GetEntityId());
		m_RunMultiplier = cmpValueModificationManager->ApplyModifications(L"UnitMotion/RunMultiplier", m_TemplateRunMultiplier, GetEntityId());

		// For MT_Deserialize compute m_Speed from the serialized m_SpeedMultiplier.
		// For MT_ValueModification and MT_OwnershipChanged, adjust m_SpeedMultiplier if needed
		// (in case then new m_RunMultiplier value is lower than the old).
		SetSpeedMultiplier(m_SpeedMultiplier);
	}

	/**
	 * Check if we are at destination early in the turn, this both lets units react faster
	 * and ensure that distance comparisons are done while units are not being moved
	 * (otherwise they won't be commutative).
	 */
	void OnTurnStart();

	void PreMove(MotionState& state);
	void Move(MotionState& state, fixed dt);
	void PostMove(MotionState& state, fixed dt);

	bool PossiblyAtDestination() const override;

	/**
	 * Process the move the unit will do this turn.
	 * This does not send actually change the position.
	 * @returns true if the move was obstructed.
	 */
	bool PerformMove(fixed dt, const fixed& turnRate,
					WaypointPath& shortPath, WaypointPath& longPath,
					MotionState& state,
					uint8_t pushingPressure) const;

	/**
	 * Update other components on our speed.
	 * (For performance, this should try to avoid sending messages).
	 */
	void UpdateMovementState(entity_pos_t speed, entity_pos_t meanSpeed);

	/**
	 * React if our move was obstructed.
	 * @param moved - true if the unit still managed to move.
	 * @returns true if the obstruction required handling, false otherwise.
	 */
	bool HandleObstructedMove(bool moved);

	/**
	 * Returns true if the target position is valid. False otherwise.
	 * (this may indicate that the target is e.g. out of the world/dead).
	 * NB: for code-writing convenience, if we have no target, this returns true.
	 */
	bool TargetHasValidPosition(const MoveRequest& moveRequest) const;
	bool TargetHasValidPosition() const
	{
		return TargetHasValidPosition(m_MoveRequest);
	}

	/**
	 * Computes the current location of our target entity (plus offset).
	 * Returns false if no target entity or no valid position.
	 */
	bool ComputeTargetPosition(CFixedVector2D& out, const MoveRequest& moveRequest) const;
	bool ComputeTargetPosition(CFixedVector2D& out) const
	{
		return ComputeTargetPosition(out, m_MoveRequest);
	}

	/**
	 * Attempts to replace the current path with a straight line to the target,
	 * if it's close enough and the route is not obstructed.
	 */
	bool TryGoingStraightToTarget(const CFixedVector2D& from, bool updatePaths);

	/**
	 * Returns whether our we need to recompute a path to reach our target.
	 */
	bool PathingUpdateNeeded(const CFixedVector2D& from) const;

	/**
	 * Rotate to face towards the target point, given the current pos
	 */
	void FaceTowardsPointFromPos(const CFixedVector2D& pos, entity_pos_t x, entity_pos_t z);

	/**
	 * Units in 'pushing' mode are marked as 'moving' in the obstruction manager.
	 * Units in 'pushing' mode should skip them in checkMovement (to enable pushing).
	 * However, units for which pushing is deactivated should collide against everyone.
	 * Units that don't block movement never participate in pushing, but they also
	 * shouldn't collide with pushing units.
	 */
	bool ShouldCollideWithMovingUnits() const
	{
		CmpPtr<ICmpPhysicalBody> cmpPhysicalBody(GetEntityHandle());
		return (!cmpPhysicalBody || !cmpPhysicalBody->IsPushingEnabled()) && m_BlockMovement;
	}

	/**
	 * Returns an appropriate obstruction filter for use with path requests.
	 */
	/**
	 * Whether other units' obstruction shapes block this unit's movement at all. They do for everyone but
	 * a solid unit (one that collides as its footprint, see PhysicalBody/Solid) that is being
	 * pushed: a unit's shape is a circle of its pathfinding clearance, which for a ship is a 10 m shore
	 * margin, so an anchored ship would wall off a 39 m square that nothing can be seen touching, and a
	 * passing ship would stop dead in open water. Solid against solid is left to pushing, which has the true
	 * shape and pushes a moving solid unit and an idle one apart.
	 */
	bool ShouldCollideWithUnits() const
	{
		CmpPtr<ICmpPhysicalBody> cmpPhysicalBody(GetEntityHandle());
		return !cmpPhysicalBody || !cmpPhysicalBody->IsPushingEnabled() ||
			!cmpPhysicalBody->GetCollisionShape().IsSolid();
	}

	ControlGroupMovementObstructionFilter GetObstructionFilter() const
	{
		return ControlGroupMovementObstructionFilter(ShouldCollideWithMovingUnits(), GetGroup(), ShouldCollideWithUnits());
	}
	/**
	 * Filter a specific tag on top of the existing control groups.
	 */
	SkipTagAndControlGroupObstructionFilter GetObstructionFilter(const ICmpObstructionManager::tag_t& tag) const
	{
		return SkipTagAndControlGroupObstructionFilter(tag, ShouldCollideWithMovingUnits(), GetGroup(),
			ShouldCollideWithUnits());
	}

	/**
	 * Decide whether to approximate the given range from a square target as a circle,
	 * rather than as a square.
	 */
	bool ShouldTreatTargetAsCircle(entity_pos_t range, entity_pos_t circleRadius) const;

	/**
	 * Create a PathGoal from a move request.
	 * @returns true if the goal was successfully created.
	 */
	bool ComputeGoal(PathGoal& out, const MoveRequest& moveRequest) const;

	/**
	 * Compute a path to the given goal from the given position.
	 * Might go in a straight line immediately, or might start an asynchronous path request.
	 */
	void ComputePathToGoal(const CFixedVector2D& from, const PathGoal& goal);

	/**
	 * Start an asynchronous long path query.
	 */
	void RequestLongPath(const CFixedVector2D& from, const PathGoal& goal);

	/**
	 * Start an asynchronous short path query.
	 * @param extendRange - if true, extend the search range to at least the distance to the goal.
	 */
	void RequestShortPath(const CFixedVector2D& from, const PathGoal& goal, bool extendRange);

	/**
	 * General handler for MoveTo interface functions.
	 */
	bool MoveTo(MoveRequest request);

	/**
	 * Convert a path into a renderable list of lines
	 */
	void RenderPath(const WaypointPath& path, std::vector<SOverlayLine>& lines, CColor color);

	void RenderSubmit(SceneCollector& collector);
};

REGISTER_COMPONENT_TYPE(UnitMotion)

bool CCmpUnitMotion::RejectFartherPaths(const PathGoal& goal, const WaypointPath& path, const CFixedVector2D& pos) const
{
	if (path.m_Waypoints.empty())
		return false;

	// Reject the new path if it does not lead us closer to the target's position.
	if (goal.DistanceToPoint(pos) <= goal.DistanceToPoint(CFixedVector2D(path.m_Waypoints.front().x, path.m_Waypoints.front().z)))
		return true;

	return false;
}

void CCmpUnitMotion::PathResult(std::uint32_t ticket, const WaypointPath& path)
{
	// Ignore obsolete path requests
	if (ticket != m_ExpectedPathTicket.m_Ticket || m_MoveRequest.m_Type == MoveRequest::NONE)
		return;

	Ticket::Type ticketType = m_ExpectedPathTicket.m_Type;
	m_ExpectedPathTicket.clear();

	// If we not longer have a position, we won't be able to do much.
	// Fail in the next Move() call.
	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (!cmpPosition || !cmpPosition->IsInWorld())
		return;
	CFixedVector2D pos = cmpPosition->GetPosition2D();

	// Assume all long paths were towards the goal, and assume short paths were if there are no long waypoints.
	bool pathedTowardsGoal = ticketType == Ticket::LONG_PATH || m_LongPath.m_Waypoints.empty();

	// Check if we need to run the short-path hack (warning: tricky control flow).
	bool shortPathHack = false;
	if (path.m_Waypoints.empty())
	{
		// No waypoints means pathing failed. If this was a long-path, try the short-path hack.
		if (!pathedTowardsGoal)
			return;
		shortPathHack = ticketType == Ticket::LONG_PATH;
	}
	else if (PathGoal goal; pathedTowardsGoal && ComputeGoal(goal, m_MoveRequest) && RejectFartherPaths(goal, path, pos))
	{
		// Reject paths that would take the unit further away from the goal.
		// This assumes that we prefer being closer 'as the crow flies' to unreachable goals.
		// This is a hack of sorts around units 'dancing' between two positions (see e.g. #3144),
		// but never actually failing to move, ergo never actually informing unitAI that it succeeds/fails.
		// (for short paths, only do so if aiming directly for the goal
		// as sub-goals may be farther than we are).

		// If this was a long-path and we no longer have waypoints, try the short-path hack.
		if (!m_LongPath.m_Waypoints.empty())
			return;
		shortPathHack = ticketType == Ticket::LONG_PATH;
	}

	// Short-path hack: if the long-range pathfinder doesn't find an acceptable path, push a fake waypoint at the goal.
	// This means HandleObstructedMove will use the short-pathfinder to try and reach it,
	// and that may find a path as the vertex pathfinder is more precise.
	if (shortPathHack)
	{
		// If we're resorting to the short-path hack, the situation is dire. Most likely, the goal is unreachable.
		// We want to find a path or fail fast. Bump failed movements so the short pathfinder will run at max-range
		// right away. This is safe from a performance PoV because it can only happen if the target is unreachable to
		// the long-range pathfinder, which is rare, and since the entity will fail to move if the goal is actually unreachable,
		// the failed movements will be increased to MAX anyways, so just shortcut.
		m_FailedMovements = MAX_FAILED_MOVEMENTS - 2;

		CFixedVector2D targetPos;
		if (ComputeTargetPosition(targetPos))
			m_LongPath.m_Waypoints.emplace_back(Waypoint{ targetPos.X, targetPos.Y });
		return;
	}

	if (ticketType == Ticket::LONG_PATH)
	{
		m_LongPath = path;
		// Long paths don't properly follow diagonals because of JPS/the grid. Since units now take time turning,
		// they can actually slow down substantially if they have to do a one navcell diagonal movement,
		// which is somewhat common at the beginning of a new path.
		// For that reason, if the first waypoint is really close, check if we can't go directly to the second.
		if (m_LongPath.m_Waypoints.size() >= 2)
		{
			const Waypoint& firstWpt = m_LongPath.m_Waypoints.back();
			if (CFixedVector2D(firstWpt.x - pos.X, firstWpt.z - pos.Y).CompareLength(Pathfinding::NAVCELL_SIZE * 4) <= 0)
			{
				CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
				ENSURE(cmpPathfinder);
				const Waypoint& secondWpt = m_LongPath.m_Waypoints[m_LongPath.m_Waypoints.size() - 2];
				// Static obstructions and terrain only: units in the way are pushed, not pathed around.
				if (cmpPathfinder->CheckStaticMovement(GetObstructionFilter(), pos.X, pos.Y, secondWpt.x, secondWpt.z, m_Clearance, m_PassClass))
					m_LongPath.m_Waypoints.pop_back();
			}

		}
	}
	else
		m_ShortPath = path;

	m_FollowKnownImperfectPathCountdown = 0;

	if (!pathedTowardsGoal)
		return;

	// Performance hack: If we were pathing towards the goal and this new path won't put us in range,
	// it's highly likely that we are going somewhere unreachable.
	// However, Move() will try to recompute the path every turn, which can be quite slow.
	// To avoid this, act as if our current path leads us to the correct destination.
	// NB: for short-paths, the problem might be that the search space is too small
	// but we'll still follow this path until the en and try again then.
	// Because we reject farther paths, it works out.
	if (PathingUpdateNeeded(pos))
	{
		// Inform other components early, as they might have better behaviour than waiting for the path to carry out.
		// Send OBSTRUCTED at first - moveFailed is likely to trigger path recomputation and we might end up
		// recomputing too often for nothing.
		if (!IncrementFailedMovementsAndMaybeNotify())
			MoveObstructed();
		// We'll automatically recompute a path when this reaches 0, as a way to improve behaviour.
		// (See D665 - this is needed because the target may be moving, and we should adjust to that).
		m_FollowKnownImperfectPathCountdown = KNOWN_IMPERFECT_PATH_RESET_COUNTDOWN;
	}
}

void CCmpUnitMotion::OnTurnStart()
{
	if (PossiblyAtDestination())
		MoveSucceeded();
	else if (!TargetHasValidPosition())
	{
		// Scrap waypoints - we don't know where to go.
		// If the move request remains unchanged and the target again has a valid position later on,
		// moving will be resumed.
		// Units may want to move to move to the target's last known position,
		// but that should be decided by UnitAI (handling MoveFailed), not UnitMotion.
		m_LongPath.m_Waypoints.clear();
		m_ShortPath.m_Waypoints.clear();

		MoveFailed();
	}
}

void CCmpUnitMotion::PreMove(MotionState& state)
{
	// Check whether our physical body takes part in pushing (the manager already holds the component pointer)
	ICmpPhysicalBody* const cmpPhysicalBody{state.cmpPhysicalBody};
	const bool pushingEnabled{cmpPhysicalBody && cmpPhysicalBody->IsPushingEnabled()};

	// Unit should be ignored by push calculations if:
	// - Pushing participation is disabled, OR
	// - Unit doesn't block movement (e.g., projectiles, birds)
	state.ignore = !pushingEnabled || !m_BlockMovement;

	// Reset state flags for this turn
	state.wasObstructed = false;
	state.wentStraight = false;
	state.poppedBlockedCorner = false;

	// Determine if this unit needs to be processed this turn
	// A unit needs update if it's in-world AND (moving OR has a pending move request OR is still rolling/spinning).
	// The roll and impact spin are integrated in PerformMove and applied in PostMove, both skipped for sleeping units:
	// otherwise a struck unit pushed into an obstacle would stop moving, go to sleep, and freeze mid-roll. All three
	// values snap to exactly zero once settled, so the unit goes back to sleep on its own.
	state.needUpdate = state.cmpPosition->IsInWorld() &&
		(m_CurrentSpeed != fixed::Zero() ||
		m_LastTurnSpeed != fixed::Zero() ||
		m_MoveRequest.m_Type != MoveRequest::NONE ||
		ImpactEffects::IsActive(state) ||
		state.hasFaceTarget); // still turning to face something (PerformMove clears it once aligned)

	if (!m_BlockMovement)
		return;

	// Set control group for formation-based force filtering
	// Units in the same formation don't apply forces to each other
	state.controlGroup = IsFormationMember() ? m_FormationController : INVALID_ENTITY;

	// Update moving flag used by force calculations
	// Note: This doesn't reflect actual movement, just intent/motion state
	state.isMoving = pushingEnabled && m_MoveRequest.m_Type != MoveRequest::NONE;

	// Update obstruction manager's moving flag for collision filtering
	CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
	if (cmpObstruction)
		cmpObstruction->SetMovingFlag(state.isMoving);
}

void CCmpUnitMotion::Move(MotionState& state, fixed dt)
{
	PROFILE("Move");

	// If we're chasing a potentially-moving unit and are currently close
	// enough to its current position, and we can head in a straight line
	// to it, then throw away our current path and go straight to it.
	state.wentStraight = TryGoingStraightToTarget(state.initialPos, true);

	uint8_t pressure = state.cmpPhysicalBody ? state.cmpPhysicalBody->GetPressure() : 0;

	state.wasObstructed = PerformMove(dt, state.cmpPosition->GetTurnRate(),
									m_ShortPath, m_LongPath,
									state,
									pressure);

	// Update inertia based on what actually happened this turn
	if (state.pos != state.initialPos)
	{
		CFixedVector2D offset = state.pos - state.initialPos;
		CFixedVector2D totalVelocity = offset.Multiply(fixed::FromInt(1) / dt);

		// Split the velocity into pathfinding and external components.
		// PerformMove stored everything external applied this turn (push + carried external inertia)
		// in externalInertiaVelocity, so pathfinding inertia is the remainder.
		CFixedVector2D ownVelocity{totalVelocity - state.externalInertiaVelocity};

		// A unit with no order has no momentum of its own to gain, only what is left of the last order's
		// to lose. Without this, a pushed unit whose step was cut short - blocked, or slid along an
		// obstacle - would have the push that never happened booked as own momentum against it, and
		// with a heavy unit's inertia keep it for seconds, walking into its neighbours on it: a herd
		// shoved about would churn on its own long after the shoving stopped. The rest of what happened
		// is the push that did.
		if (!IsMoveRequested())
		{
			ownVelocity = MomentumModel::ClampSpeed(ownVelocity, DecayCoastingSpeed(state.inertiaVelocity.Length(), dt));
			state.externalInertiaVelocity = totalVelocity - ownVelocity;
		}
		state.inertiaVelocity = ownVelocity;
	}
	else if (state.wasObstructed)
	{
		state.inertiaVelocity = state.inertiaVelocity.Multiply(fixed::FromFloat(0.5f));
		state.externalInertiaVelocity = state.externalInertiaVelocity.Multiply(fixed::FromFloat(0.5f));
	}
	else
	{
		// Standing still: momentum decays as it does while drifting. Kept forever, it would be resurrected by the next
		// order's blend, and a unit "starting" to move would already be at speed.
		state.inertiaVelocity = IsMoveRequested() ?
			state.inertiaVelocity.Multiply(PushConfig::DecayFactor(GetInertiaKeep(), dt)) :
			MomentumModel::ClampSpeed(state.inertiaVelocity, DecayCoastingSpeed(state.inertiaVelocity.Length(), dt));
	}

	// Negligible inertia doesn't decay away on its own (see SnapNegligible): without this, units that finished moving
	// would keep creeping by a raw fixed-point unit per turn and never go back to sleep.
	// Without a move request, stop as soon as the glide left isn't visible (see CoastingRestSpeed). Not with one: a slow
	// unit accelerating from rest builds its inertia from speeds that low, and would never get going.
	state.inertiaVelocity = IsMoveRequested() ? MomentumModel::SnapNegligible(state.inertiaVelocity) :
		MomentumModel::SnapBelow(state.inertiaVelocity,
			MomentumModel::CoastingRestSpeed(PushConfig::DecayFactor(GetInertiaKeep(), dt), dt));
}

void CCmpUnitMotion::PostMove(MotionState& state, fixed dt)
{
	// Update our speed over this turn so that the visual actor shows the correct animation.
	// The heading was already rate-limited by the heading controller in PerformMove, so it's applied as is.
	// Moving and turning in a single call advertises the position change once, not once per call: every
	// MT_PositionChanged subscriber (obstructions, range queries, vision...) handles it right away.
	if (state.pos == state.initialPos)
	{
		if (state.angle != state.initialAngle)
			state.cmpPosition->TurnTo(state.angle);
		UpdateMovementState(fixed::Zero(), fixed::Zero());
	}
	else
	{
		state.cmpPosition->MoveAndTurnTo(state.pos.X, state.pos.Y, state.angle);

		// Calculate the mean speed over this past turn.
		CFixedVector2D offset = state.pos - state.initialPos;
		UpdateMovementState(state.speed, offset.Length() / dt);
	}

	// Apply roll
	if (ImpactEffects::HasRoll(state))
	{
		ImpactEffects::SettleRoll(state);
		state.cmpPosition->SetRoll(state.rollAngle);
	}

	// Braking can legitimately leave us with a speed that isn't quite zero, so "our position changed" is not
	// the same as "we got somewhere": a unit inching forward by a fraction of a navcell per turn never reaches
	// its waypoint, never counts as obstructed, and so never reaches the failed-movement handling that would
	// repath it or eventually tell UnitAI to give up - meanwhile it keeps resetting the counter below, so it
	// stays that way forever. Hold it to real progress instead.
	// Two things excuse standing still: turning in place, which is deliberate and takes a high-inertia unit
	// several turns, and waiting on a path we've asked for and not yet been given. Anything else is stuck -
	// with no waypoints at all most of all, since nothing will move it until something asks for a path for it.
	const bool madeProgress{(state.pos - state.initialPos).CompareLength(MinProgressSpeed().Multiply(dt)) >= 0};
	const bool hasPath{!m_ShortPath.m_Waypoints.empty() || !m_LongPath.m_Waypoints.empty()};
	const bool awaitingFirstPath{!hasPath && m_ExpectedPathTicket.m_Ticket != 0};
	const bool turning{(state.angle - state.initialAngle).Absolute() >= MIN_ROTATION_SPEED.Multiply(dt)};

	if (!madeProgress && IsMoveRequested() && !state.wasObstructed && !awaitingFirstPath && !turning)
		state.wasObstructed = true;

	if (state.wasObstructed && HandleObstructedMove(madeProgress))
		return;
	else if (!state.wasObstructed && madeProgress)
		m_FailedMovements = 0;

	// PathingUpdateNeeded asks whether our path is still good enough. With no path at all it compares our shape
	// against the goal's, so it can answer "close enough" while PossiblyAtDestination, which decides whether we've
	// arrived, says we haven't (for a point or a formation slot it checks our position against the point).
	// A unit caught between the two holds an order but has no waypoints and no path request: ask for a path now,
	// rather than let it coast to a stop and get one only once the check above counts it as stuck.
	// Only with a move request, since PossiblyAtDestination is always false without one.
	const bool needPathUpdate{PathingUpdateNeeded(state.pos) ||
		(IsMoveRequested() && !hasPath && m_ExpectedPathTicket.m_Ticket == 0 && !PossiblyAtDestination())};

	// If we popped a corner waypoint although the straight line to the next target is obstructed
	// (we couldn't reach it without circling back), path around the obstruction from here
	// instead of heading straight into it.
	// Not while a long path request is pending: we wait for one path at a time, so this would drop it. Its new
	// waypoints won't fix the corner either - we're sitting on it, so their waypoint there is usually popped the same
	// way - but once they arrive this runs again and paths around it.
	if (!needPathUpdate && !state.wasObstructed && state.poppedBlockedCorner &&
		(m_ExpectedPathTicket.m_Ticket == 0 || m_ExpectedPathTicket.m_Type == Ticket::SHORT_PATH))
	{
		if (!m_LongPath.m_Waypoints.empty())
		{
			const PathGoal subgoal{PathGoal::CIRCLE, m_LongPath.m_Waypoints.back().x, m_LongPath.m_Waypoints.back().z,
				Pathfinding::NAVCELL_SIZE * 2};
			RequestShortPath(state.pos, subgoal, false);
		}
		else
		{
			PathGoal goal;
			if (ComputeGoal(goal, m_MoveRequest))
				RequestShortPath(state.pos, goal, false);
		}
	}
	// If we're following a long-path, check if we might run into units in advance, to smoothe motion.
	else if (!needPathUpdate && !state.wasObstructed && m_LongPath.m_Waypoints.size() >= 1 && m_ShortPath.m_Waypoints.empty())
	{
		ICmpObstructionManager::tag_t specificIgnore;
		if (m_MoveRequest.m_Type == MoveRequest::ENTITY)
		{
			CmpPtr<ICmpObstruction> cmpTargetObstruction(GetSimContext(), m_MoveRequest.m_Entity);
			if (cmpTargetObstruction)
				specificIgnore = cmpTargetObstruction->GetObstruction();
		}
		CmpPtr<ICmpObstructionManager> cmpObstructionManager(GetSystemEntity());
		if (cmpObstructionManager && cmpObstructionManager->TestUnitLine(GetObstructionFilter(specificIgnore),
			state.pos.X, state.pos.Y, m_LongPath.m_Waypoints.back().x, m_LongPath.m_Waypoints.back().z, m_Clearance, true))
		{
			// We will run into something: request a new short path.
			// If we have several waypoints left, aim for the one after next directly.
			// (This is mostly because the obstruction might be at the waypoint, and the end is kind of treated specially).
			// Else just path to the goal.
			if (m_LongPath.m_Waypoints.size() > 1) {
				fixed radius = Pathfinding::NAVCELL_SIZE * 2;
				m_LongPath.m_Waypoints.pop_back();
				PathGoal subgoal = { PathGoal::CIRCLE, m_LongPath.m_Waypoints.back().x, m_LongPath.m_Waypoints.back().z, radius };
				RequestShortPath(state.pos, subgoal, false);
			} else {
				// If we only have one waypoint left, request a short path to the waypoint itself.
				PathGoal goal;
				if (ComputeGoal(goal, m_MoveRequest))
					RequestShortPath(state.pos, goal, false);
			}
		}
	}

	// If we moved straight, and didn't quite finish the path, reset - we'll update it next turn if still OK.
	if (state.wentStraight && !state.wasObstructed)
		m_ShortPath.m_Waypoints.clear();

	// We may need to recompute our path sometimes (e.g. if our target moves).
	// Since we request paths asynchronously anyways, this does not need to be done before moving.
	if (!state.wentStraight && needPathUpdate)
	{
		PathGoal goal;
		if (ComputeGoal(goal, m_MoveRequest))
			ComputePathToGoal(state.pos, goal);
	}
	else if (m_FollowKnownImperfectPathCountdown > 0)
		--m_FollowKnownImperfectPathCountdown;

	// NB: there's deliberately no displacement-based path re-evaluation here. Being carried away from the
	// current waypoint is what turning and drifting look like, so it can't tell a push from a manoeuvre.
	// Repathing is driven by events instead: PathingUpdateNeeded (the goal moved), HandleObstructedMove
	// (movement actually blocked), the TestUnitLine look-ahead (units in the way) and poppedBlockedCorner.
	// A pushed unit keeps its path and steers at the same waypoint from its new position; if the push put
	// an obstacle in between, the next move is obstructed and that handling takes over.
}

bool CCmpUnitMotion::PossiblyAtDestination() const
{
	if (m_MoveRequest.m_Type == MoveRequest::NONE)
		return false;

	CmpPtr<ICmpObstructionManager> cmpObstructionManager(GetSystemEntity());
	ENSURE(cmpObstructionManager);

	switch (m_MoveRequest.m_Type)
	{
	case MoveRequest::POINT:
		return cmpObstructionManager->IsInPointRange(
			GetEntityId(), m_MoveRequest.m_Position.X, m_MoveRequest.m_Position.Y,
			m_MoveRequest.m_MinRange, m_MoveRequest.m_MaxRange, false);

	case MoveRequest::ENTITY:
		return cmpObstructionManager->IsInTargetRange(
			GetEntityId(), m_MoveRequest.m_Entity,
			m_MoveRequest.m_MinRange, m_MoveRequest.m_MaxRange, false);

	case MoveRequest::OFFSET:
	{
		CmpPtr<ICmpUnitMotion> cmpControllerMotion(GetSimContext(), m_MoveRequest.m_Entity);
		if (cmpControllerMotion && cmpControllerMotion->IsMoveRequested())
			return false;

		CFixedVector2D targetPos;
		ComputeTargetPosition(targetPos);
		CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
		return (targetPos - cmpPosition->GetPosition2D()).CompareLength(Pathfinding::NAVCELL_SIZE) <= 0;
	}

	default:
		return false;
	}
}

bool CCmpUnitMotion::PerformMove(fixed dt, const fixed& turnRate,
                                WaypointPath& shortPath, WaypointPath& longPath,
                                MotionState& state,
                                uint8_t pushingPressure) const
{
	const ICmpPathfinder* const cmpPathfinder{
		static_cast<const ICmpPathfinder*>(QueryInterface(GetSimContext(), SYSTEM_ENTITY, IID_Pathfinder))};
	ENSURE(cmpPathfinder);

    CFixedVector2D& pos = state.pos;
    fixed& speed = state.speed;
    entity_angle_t& angle = state.angle;
    const CFixedVector2D& inertiaVelocity = state.inertiaVelocity;
    const CFixedVector2D& externalVelocity = state.externalVelocity;

	fixed basicSpeed = m_Speed;
	// If in formation, run to keep up; otherwise just walk.
	if (IsMovingAsFormation())
		basicSpeed = m_Speed.Multiply(m_RunMultiplier);

	// If pushing pressure is applied, slow the unit down.
	if (pushingPressure)
	{
		// Values below this pressure don't slow the unit down (avoids slowing groups down).
		constexpr int pressureMinThreshold = 10;

		// Lower speed up to a floor to prevent units from getting stopped
		constexpr int maxPressure = PushConfig::MAX_PRESSURE - pressureMinThreshold - 80;
		constexpr entity_pos_t floorSpeed = entity_pos_t::FromFraction(3, 2);
		static_assert(maxPressure > 0);

		uint8_t slowdown = maxPressure - std::min(maxPressure, std::max(0, pushingPressure - pressureMinThreshold));
		basicSpeed = basicSpeed.Multiply(fixed::FromInt(slowdown) / maxPressure);
		// NB: lowering this too much will make the units behave a lot like viscous fluid
		// when the density becomes extreme. While perhaps realistic (and kind of neat),
		// it's not very helpful for gameplay. Empirically, a value of 1.5 avoids most of the effect
		// while still slowing down movement significantly, and seems like a good balance.
		// Min with the template speed to allow units that are explicitly absurdly slow.
		basicSpeed = std::max(std::min(m_TemplateWalkSpeed, floorSpeed), basicSpeed);
	}

	// TODO: would be nice to support terrain-dependent speed again.
	fixed maxSpeed = basicSpeed;

	// ========================================
	// STAGGER EFFECT - Reduces speed, turning, and acceleration
	// ========================================
	fixed staggerFactor = fixed::FromInt(1);
	fixed turnReduction = fixed::FromInt(1);
	fixed speedReduction = fixed::FromInt(1);

	if (state.stagger > fixed::Zero())
	{
		// Pathfinding control reduction
		staggerFactor = fixed::FromInt(1) - state.stagger;

		// Turn rate reduction
		turnReduction = fixed::FromInt(1) - state.stagger;

		// Speed reduction (max speed is lowered)
		speedReduction = fixed::FromInt(1) - state.stagger;

		// Apply speed reduction to max speed
		maxSpeed = maxSpeed.Multiply(speedReduction);
	}

	// Calculate desired velocity from pathfinding
	CFixedVector2D pathfindingVelocity;
	const bool hasPathfinding{!shortPath.m_Waypoints.empty() || !longPath.m_Waypoints.empty()};

	// The waypoint we're heading for (there is one exactly when hasPathfinding), and the way to it.
	// pos doesn't change before the move is applied, so everything below shares these.
	const std::optional<CFixedVector2D> currentWaypoint{PathFollowing::CurrentWaypoint(shortPath, longPath)};
	const CFixedVector2D toWaypoint{currentWaypoint ? *currentWaypoint - pos : CFixedVector2D{}};

	// Charging a melee target on the last leg: keep full speed, so the collision carries the momentum.
	// Earlier legs still brake for their corners.
	// Known limitation: arriving at full speed, a charger can end up out of attack range again - by coasting after
	// the move ends (Drifting > 0, e.g. ram ships), by stepping over the whole range band in one long turn, or by
	// the charge knock-back itself separating the units. UnitAI then gets OutOfRange, chases, and charges again.
	const bool chargingFinalLeg{m_MoveRequest.m_Type == MoveRequest::ENTITY && m_MoveRequest.m_Charge &&
		hasPathfinding && !PathFollowing::NextWaypoint(shortPath, longPath)};

	// Where the unit is pointing. Steering and drift correction both work relative to it, and the heading
	// only changes further down, so the sine and cosine behind it are worth paying for just once.
	const CFixedVector2D facingDir{HeadingControl::FacingDirection(angle)};

	if (hasPathfinding)
	{
		// Steer for the current waypoint, less effectively while staggered.
		// Collision avoidance uses last turn's prediction, as the prediction runs after this point.
		pathfindingVelocity = PathFollowing::SteeringVelocity(toWaypoint, facingDir, speed, maxSpeed, m_TemplateInertia,
			m_TemplateTurnBraking, state.predictedCollision).Multiply(staggerFactor);
	}

	// ========================================
	// STEP 1: PREDICTIVE COLLISION AVOIDANCE
	// Check if inertia will cause a collision, and if so,
	// prepare to rely more on pathfinding
	// ========================================
	bool hasMoveRequest = IsMoveRequested();
	fixed inertiaStrength = m_TemplateInertia;
	fixed effectiveDriftCorrection = m_TemplateDriftCorrection; // Will be boosted if collision predicted

	// Ignore our target's obstruction when checking movement, if we're moving to an entity.
	const ICmpObstructionManager::tag_t specificIgnore{[this]() -> ICmpObstructionManager::tag_t {
		if (m_MoveRequest.m_Type == MoveRequest::ENTITY)
		{
			CmpPtr<ICmpObstruction> cmpTargetObstruction(GetSimContext(), m_MoveRequest.m_Entity);
			if (cmpTargetObstruction)
				return cmpTargetObstruction->GetObstruction();
		}
		return {};
	}()};

	// Recomputed just below; the steering velocity above deliberately used last turn's value.
	state.predictedCollision = false;

	if (hasPathfinding && hasMoveRequest && !inertiaVelocity.IsZero())
	{
		const CFixedVector2D predictedPos{MomentumModel::PredictPosition(pos, pathfindingVelocity, inertiaVelocity,
			inertiaStrength, maxSpeed, dt)};

		// Test if this multi-turn prediction collides with terrain or static obstructions. Units are left out:
		// pushing resolves unit contacts, and treating an idle unit ahead (e.g. a charge target) as a wall
		// maxed out drift correction for no reason.
		const bool willCollide{!cmpPathfinder->CheckStaticMovement(
			GetObstructionFilter(specificIgnore),
			pos.X, pos.Y,
			predictedPos.X, predictedPos.Y,
			m_Clearance, m_PassClass)};

		state.predictedCollision = willCollide;

		// Boost drift correction to maximum.
		if (willCollide)
			effectiveDriftCorrection = fixed::FromInt(1);
	}

	// ========================================
	// STEP 2: BLEND PATHFINDING WITH INERTIA
	// Combine desired movement direction with momentum
	// ========================================
	CFixedVector2D combinedVelocity;

	// External inertia (from past pushes) carries over and decays by the drifting factor,
	// whether or not the unit is actively moving. Drifting is per IMPACT_REFERENCE_TIME, not per turn.
	const fixed driftKeep{PushConfig::DecayFactor(m_TemplateDrifting, dt)};
	const CFixedVector2D externalCarry{state.externalInertiaVelocity.Multiply(driftKeep)};
	// The heading's share of that carried push momentum, decaying by the same factor.
	const fixed pushSpinCarry{state.pushSpin.Multiply(driftKeep)};

	// combinedVelocity is our own movement until the external forces are added at the end: the unit steers and brakes
	// what it controls, but can't steer or brake against being pushed.
	if (hasMoveRequest && hasPathfinding)
	{
		combinedVelocity = MomentumModel::ClampSpeed(
			MomentumModel::BlendWithInertia(pathfindingVelocity, inertiaVelocity, inertiaStrength), maxSpeed);
	}
	else
	{
		// No path to follow - just drift with damping. With no order at all, stop as soon as the glide left isn't
		// visible (see CoastingRestSpeed). With one - waiting for a path, or the turn after going straight, which
		// clears the short path - keep drifting, so a slow unit isn't stopped dead in the middle of its order.
		const fixed coastKeep{PushConfig::DecayFactor(GetInertiaKeep(), dt)};
		if (hasMoveRequest)
			combinedVelocity = MomentumModel::SnapNegligible(inertiaVelocity.Multiply(coastKeep));
		else
		{
			const CFixedVector2D coasting{MomentumModel::SnapBelow(inertiaVelocity, MomentumModel::CoastingRestSpeed(coastKeep, dt))};
			combinedVelocity = MomentumModel::SnapNegligible(
				MomentumModel::ClampSpeed(coasting, DecayCoastingSpeed(coasting.Length(), dt)));
		}
	}

	// Set when our own braking - the drift correction cost below, or the turn cap further down - brings us
	// to a full stop. That's deliberate, so PerformMove must not report it as an obstruction.
	bool stoppedToTurn{false};

	// ========================================
	// STEP 3: DRIFT CORRECTION (After blending)
	// Reduce lateral movement at the cost of speed
	// ========================================
	if (hasMoveRequest && hasPathfinding &&
		!inertiaVelocity.IsZero() && effectiveDriftCorrection > fixed::Zero())
		stoppedToTurn = MomentumModel::CorrectDrift(combinedVelocity, inertiaVelocity, facingDir, effectiveDriftCorrection, maxSpeed);

	// Speed carried by momentum this turn, before the unit's own braking below.
	// The waypoint acceptance radius uses it, so braking for a waypoint doesn't shrink that waypoint's radius.
	// combinedVelocity isn't touched again until the brake multipliers are applied, so every speed needed
	// between here and there is this one: reuse it rather than pay another isqrt64 per site.
	const fixed unbrakedSpeed{combinedVelocity.Length()};

	// 1. PATH TURN: How much the unit needs to turn to face the waypoint (for angular momentum)
	entity_angle_t pathDesiredAngle = angle;
	fixed pathNeededTurn = fixed::Zero();

	if (hasPathfinding && hasMoveRequest)
	{
		// Not normalized first: an angle doesn't depend on the length, and scaling down to a unit vector
		// throws away precision here rather than adding any - a direction under one metre keeps only its
		// fractional bits, where the raw offset still has its whole ones. It also costs a square root.
		if (!toWaypoint.IsZero())
		{
			pathDesiredAngle = atan2_approx(toWaypoint.X, toWaypoint.Y);
			pathNeededTurn = HeadingControl::WrapAngle(pathDesiredAngle - angle).Absolute();
		}
	}

	// 2. MOVEMENT TURN: How much the movement direction differs from current facing (for slowdown)
	entity_angle_t movementDesiredAngle = angle;
	fixed movementNeededTurn = fixed::Zero();

	if (unbrakedSpeed > fixed::FromFloat(0.1f))
	{
		movementDesiredAngle = atan2_approx(combinedVelocity.X, combinedVelocity.Y);
		movementNeededTurn = HeadingControl::WrapAngle(movementDesiredAngle - angle).Absolute();
	}

	// ========================================
	// TURN SLOWDOWN + PROXIMITY BRAKING
	// ========================================

	// NB: proximity braking lives here too, so this isn't gated on the turn braking:
	// a unit that never slows down for turns still has to slow down near its waypoints.
	if (hasMoveRequest && hasPathfinding && !combinedVelocity.IsZero())
	{
		// 1. TURN SLOWDOWN
		// Slow down for the turn we still have to make (facing to waypoint), not only for how far we are
		// already skidding sideways: drift correction removes that skid, so keying off it alone would mean
		// a high DriftCorrection cancels the slowdown. Keep the skid as well, so sliding units also brake.
		fixed turnBrakeMultiplier{fixed::FromInt(1)};
		if (const std::optional<fixed> turnSpeedCap{PathFollowing::TurnSpeedCap(std::max(pathNeededTurn, movementNeededTurn),
				m_TemplateTurnBraking, maxSpeed)})
		{
			if (unbrakedSpeed > *turnSpeedCap && unbrakedSpeed > fixed::Epsilon())
				turnBrakeMultiplier = *turnSpeedCap / unbrakedSpeed;

			// A unit told to stop and turn isn't stuck: it still rotates below, and PerformMove must not
			// report that as an obstruction.
			stoppedToTurn = stoppedToTurn || *turnSpeedCap <= fixed::Zero();
		}

		// 2. PROXIMITY BRAKING
		// Slow down near the waypoint, more so when not heading straight at it. The turning circle grows with speed
		// (radius = speed / turn rate): a unit arriving too fast can't turn tightly enough to reach its waypoint, and
		// circles it instead.
		fixed proximityBrakeMultiplier{fixed::FromInt(1)};
		if (currentWaypoint)
		{
			const fixed distanceToWaypoint{toWaypoint.Length()};
			const fixed alignmentToWaypoint{distanceToWaypoint > fixed::Epsilon() ?
				PathFollowing::Alignment(combinedVelocity, unbrakedSpeed, toWaypoint, distanceToWaypoint) :
				fixed::Zero()};

			// Higher inertia brakes earlier. No braking has been applied yet, so this is unbrakedSpeed.
			const fixed brakingDistance{PathFollowing::StoppingDistance(unbrakedSpeed, dt, m_TemplateInertia)};

			// How sharply the path turns at this waypoint (1 = straight on, 0 = right angle or sharper).
			// Only a corner we can't cut counts: if the straight line to the next waypoint is clear,
			// the waypoint will be popped and the corner cut, so there's nothing to brake for
			// (e.g. unoptimized paths in open terrain).
			// Falls back to our alignment to the waypoint for the last waypoint.
			fixed cornerAlignment{alignmentToWaypoint};
			if (const std::optional<CFixedVector2D> nextWaypoint{PathFollowing::NextWaypoint(shortPath, longPath)};
				nextWaypoint && distanceToWaypoint > fixed::Epsilon() && distanceToWaypoint < brakingDistance)
			{
				const CFixedVector2D nextLeg{*nextWaypoint - *currentWaypoint};
				if (!nextLeg.IsZero())
				{
					const fixed geometricCorner{PathFollowing::Alignment(toWaypoint, distanceToWaypoint,
						nextLeg, nextLeg.Length())};

					// Only test the line when the corner would brake more than our heading error does.
					// Only terrain and static obstructions make a corner uncuttable: pushing resolves units.
					if (geometricCorner < alignmentToWaypoint &&
						!cmpPathfinder->CheckStaticMovement(GetObstructionFilter(specificIgnore),
							pos.X, pos.Y, nextWaypoint->X, nextWaypoint->Y, m_Clearance, m_PassClass))
						cornerAlignment = geometricCorner;
				}
			}

			// Brake both for heading error (drifting off the waypoint direction) and for a sharp corner.
			// Passing a waypoint also lowers alignmentToWaypoint, but the turning-circle pop handles that case.
			if (distanceToWaypoint < brakingDistance && !chargingFinalLeg)
				proximityBrakeMultiplier = PathFollowing::ProximityBrakeScale(distanceToWaypoint, brakingDistance,
					unbrakedSpeed, std::min(alignmentToWaypoint, cornerAlignment));
		}

		combinedVelocity = combinedVelocity.Multiply(turnBrakeMultiplier.Multiply(proximityBrakeMultiplier));
	}

	// PREDICTIVE BRAKING: stop where the move ends, as far ahead as inertia takes to stop. That is short of the end
	// of the path by StoppingMargin(), so the unit is at a crawl when its order ends rather than at speed.
	if (hasMoveRequest && hasPathfinding && !combinedVelocity.IsZero() && !chargingFinalLeg)
		if (const std::optional<CFixedVector2D> finalWaypoint{PathFollowing::FinalWaypoint(shortPath, longPath)})
		{
			const CFixedVector2D toEnd{*finalWaypoint - pos};
			const fixed margin{StoppingMargin()};

			// Only the last stopping distance needs braking. On this branch combinedVelocity is at most maxSpeed
			// (it's clamped when blended, and every multiplier since is at most 1), so its stopping distance is at
			// most the one at maxSpeed. Comparing squares against that skips both square roots below for units
			// still far from the end of their path.
			const fixed maxStoppingDistance{PathFollowing::StoppingDistance(maxSpeed, dt, m_TemplateInertia)};
			if (toEnd.CompareLength(maxStoppingDistance + margin) < 0)
				if (const std::optional<fixed> brakeFactor{PathFollowing::PredictiveBrakeFactor(
						std::max(toEnd.Length() - margin, fixed::Zero()), combinedVelocity.Length(), maxSpeed,
						maxStoppingDistance, dt)})
					combinedVelocity = combinedVelocity.Multiply(*brakeFactor);
		}

	// PATHFINDING ANGULAR MOMENTUM
	// Apply turn reduction when staggered
	const fixed effectiveTurnRate{turnRate.Multiply(turnReduction)};
	const fixed effectiveTurnAcceleration{m_TemplateTurnAcceleration.Multiply(turnReduction)};

	if (hasPathfinding && hasMoveRequest)
	{
		// Face the waypoint, pulled towards the direction of travel as far as drift correction allows.
		// pathDesiredAngle is already the waypoint's heading (or our own, standing on it).
		const entity_angle_t desiredAngle{HeadingControl::PathFacingAngle(pathDesiredAngle,
			pathfindingVelocity + inertiaVelocity.Multiply(inertiaStrength), fixed::FromInt(1) - effectiveDriftCorrection)};

		// Turn with angular momentum, the same rule as every other heading change.
		HeadingControl::TurnWithMomentum(angle, state.angularVelocity, desiredAngle, effectiveTurnRate,
			effectiveTurnAcceleration, dt);
	}
	else if (hasMoveRequest && !hasPathfinding)
	{
		// A move request without a path yet (e.g. the first turns of a new order, while the path is computed):
		// face the move target, with angular momentum like every other turn.
		CFixedVector2D targetPos;
		if (ComputeTargetPosition(targetPos))
		{
			const CFixedVector2D toTarget{targetPos - pos};
			if (!toTarget.IsZero())
				HeadingControl::TurnWithMomentum(angle, state.angularVelocity, atan2_approx(toTarget.X, toTarget.Y), effectiveTurnRate,
					effectiveTurnAcceleration, dt);
		}
	}
	else if (!hasMoveRequest && state.hasFaceTarget)
	{
		// Keep turning towards a heading set by FaceTowardsPoint (an attack target, or our destination on
		// arrival) until aligned, with angular momentum. The unit is kept awake meanwhile (see PreMove).
		if (HeadingControl::TurnWithMomentum(angle, state.angularVelocity, state.faceTargetAngle, effectiveTurnRate,
				effectiveTurnAcceleration, dt))
			state.hasFaceTarget = false;
	}

	// Impact spin (from charges and other external forces), on top of the unit's own turning, then roll.
	// NB: angle is state.angle, which IntegrateSpin rotates.
	ImpactEffects::IntegrateSpin(state, dt);
	ImpactEffects::IntegrateRoll(state, dt);

	// Spin from collision pushes: what was carried in plus what this turn's pushes added. Kept apart from
	// the impact spin above because it follows the push momentum's decay (Drifting) rather than spinDecay,
	// so a unit whose pushes are one-shot can't be wound up by being pushed every turn.
	const fixed pushSpinTotal{pushSpinCarry + state.impulseSpin};
	if (!pushSpinTotal.IsZero())
		state.angle = HeadingControl::WrapAngle(state.angle + pushSpinTotal.Multiply(dt));

	// The unit's own movement, before the external forces below are added: what it falls back on if the step with
	// them in it is blocked.
	const CFixedVector2D ownVelocity{combinedVelocity};

	// Add external forces: carried-over momentum and this turn's pushes
	combinedVelocity = combinedVelocity + externalCarry + externalVelocity;
	speed = combinedVelocity.Length();

	// Everything external applied this turn becomes next turn's external inertia, spin included. Pushes
	// and hits carry on as momentum, decaying by Drifting. Nothing bounds the total here: a soft push is
	// bounded by the pushing rate, and a hit by the closing speed or the attack's Knockback. A light unit
	// rammed at speed is meant to be flung faster than it can move.
	state.externalInertiaVelocity = MomentumModel::CarryExternalInertia(externalCarry, externalVelocity);
	state.pushSpin = MomentumModel::CarryPushSpin(pushSpinCarry, state.impulseSpin);

	// Stopping to turn in place is deliberate, not an obstruction: the facing was already rotated above.
	if (combinedVelocity.IsZero())
		return hasPathfinding && !stoppedToTurn;

	CFixedVector2D movement = combinedVelocity.Multiply(dt);
	CFixedVector2D target = pos + movement;

	bool canMove{cmpPathfinder->CheckMovement(GetObstructionFilter(specificIgnore),
		pos.X, pos.Y, target.X, target.Y, m_Clearance, m_PassClass)};

	// The step is tested as one line with the pushes in it, so a unit pressed sideways into an obstacle by its
	// neighbours fails it every turn, however clear its own way is, and never moves again. A push can't shove a
	// unit into what it can't walk into, and it mustn't cost it its own step either: a blocked step is tried
	// again without the pushes, and if that goes, the push is lost, momentum and all, as a push into a wall is.
	if (!canMove && !ownVelocity.IsZero() && ownVelocity != combinedVelocity)
	{
		const CFixedVector2D ownTarget{pos + ownVelocity.Multiply(dt)};
		if (cmpPathfinder->CheckMovement(GetObstructionFilter(specificIgnore),
				pos.X, pos.Y, ownTarget.X, ownTarget.Y, m_Clearance, m_PassClass))
		{
			target = ownTarget;
			combinedVelocity = ownVelocity;
			speed = ownVelocity.Length();
			state.externalInertiaVelocity = CFixedVector2D();
			canMove = true;
		}
	}

	if (canMove)
	{
		pos = target;
		// ========================================
		// WAYPOINT POPPING WITH OBSTRUCTION CHECK
		// ========================================

		// Both tests below answer "no" beyond this: the acceptance radius is at most the stopping distance at max
		// speed times a scale under 2 (see AcceptanceRadius), and CannotReach is only asked within that distance.
		const fixed popRange{PathFollowing::StoppingDistance(maxSpeed, dt, m_TemplateInertia) * 2};

		while (const std::optional<CFixedVector2D> currentWaypoint{PathFollowing::CurrentWaypoint(shortPath, longPath)})
		{
			const CFixedVector2D waypoint{*currentWaypoint};
			const std::optional<CFixedVector2D> nextWaypoint{PathFollowing::NextWaypoint(shortPath, longPath)};
			const CFixedVector2D toWaypoint{waypoint - pos};
			// Whether the unit is already past this waypoint, towards the next one. A unit pushed past its waypoint
			// by others then goes on to the next, instead of turning round to go back for it.
			const bool passed{nextWaypoint && PathFollowing::PassedWaypoint(pos, waypoint, *nextWaypoint)};

			// Too far to pop, so don't pay for the two tests below - nor for the square root, when comparing
			// squares already settles it. Length() rounds its root down, so only a margin of one ulp proves
			// the strict comparison: at exactly popRange² the floored root can still land on popRange.
			if (!passed && toWaypoint.CompareLength(popRange + fixed::Epsilon()) >= 0)
				break;

			const fixed distanceToWaypoint{toWaypoint.Length()};
			if (!passed && distanceToWaypoint > popRange)
				break;

			// The end of the path is what the unit brakes for, so it is kept until the unit is on it, or within
			// its StoppingMargin() of it and already in range of the goal.
			const bool isCloseEnough{nextWaypoint ?
				distanceToWaypoint <= PathFollowing::AcceptanceRadius(pos, waypoint, nextWaypoint, combinedVelocity, speed,
					distanceToWaypoint, unbrakedSpeed, maxSpeed, m_TemplateInertia, effectiveDriftCorrection, dt) :
				distanceToWaypoint <= Pathfinding::NAVCELL_SIZE ||
					(distanceToWaypoint <= StoppingMargin() && PossiblyAtDestination())};
			// Can we still reach this waypoint, or would we have to circle back to it?
			// Only asked within our stopping distance at max speed. Never for the end of the path, which the unit
			// brakes onto: giving up on it would end the move at speed.
			const bool cannotReach{nextWaypoint && PathFollowing::CannotReach(pos, waypoint, combinedVelocity, speed,
				distanceToWaypoint, angle, effectiveTurnRate, effectiveTurnAcceleration,
				PathFollowing::StoppingDistance(maxSpeed, dt, m_TemplateInertia))};

			// Pop it once we're close enough, once we're past it, or as soon as we know we can't reach it without circling
			if (!isCloseEnough && !cannotReach && !passed)
				break;

			// Before popping the waypoint, check that we can go straight on to what comes after it: the next waypoint,
			// or the final goal if this is the last waypoint overall. This prevents cutting corners around
			// terrain/obstructions. Units don't count: pushing resolves them, and an idle unit near a corner
			// would otherwise keep the waypoint, forcing pops and short path requests.
			std::optional<CFixedVector2D> nextTarget{nextWaypoint};
			if (!nextTarget)
			{
				PathGoal finalGoal;
				if (ComputeGoal(finalGoal, m_MoveRequest))
					nextTarget = finalGoal.NearestPointOnGoal(pos);
			}

			if (nextTarget && !cmpPathfinder->CheckStaticMovement(GetObstructionFilter(specificIgnore),
					pos.X, pos.Y, nextTarget->X, nextTarget->Y, m_Clearance, m_PassClass))
			{
				// The line is obstructed: keep the waypoint as a corner point, unless
				// - we're extremely close to it (within 2 navcells), so we don't circle it;
				// - or we can't reach it without circling back: orbiting a corner is worse than letting obstruction
				//   handling deal with it.

				// ... And only if we're actually travelling. Circling is a problem for a unit with speed; a stopped
				// one that pops waypoints it never reached just deletes its own path and asks for a new one from the
				// same spot, turn after turn. The acceptance radius is deliberately computed from the unbraked speed,
				// so a braked-to-a-crawl unit "reaches" waypoints over a metre away without moving at all.
				const bool travelling{speed >= MinProgressSpeed()};
				const bool canPop{distanceToWaypoint <= Pathfinding::NAVCELL_SIZE / 2 ||
					(travelling && (distanceToWaypoint <= Pathfinding::NAVCELL_SIZE * 2 || cannotReach))};
				// We're popping onto an obstructed line: PostMove will path around it.
				state.poppedBlockedCorner = canPop;
				if (!canPop)
					break;
			}

			if (shortPath.m_Waypoints.empty())
				longPath.m_Waypoints.pop_back();
			else
				shortPath.m_Waypoints.pop_back();
		}
		// Movement succeeded - no obstruction
		return false;
	}
	else
	{
		// Movement was obstructed
		const CFixedVector2D originalPos{pos};
		const CFixedVector2D originalMovement{target - pos};

		if (!hasPathfinding)
		{
			// No path to follow (pushed, drifting, or waiting for a path): slide along the obstacle.
			if (ObstructionResolution::TrySlide(pos, originalMovement, GetEntityId(), *cmpPathfinder,
					GetObstructionFilter(specificIgnore), m_Clearance, m_PassClass))
				return false;
		}
		else
		{
			// Following a path: step sideways around the obstacle, keeping the current waypoint in sight.
			if (ObstructionResolution::TryPerpendicularNudge(pos, originalMovement,
					PathFollowing::CurrentWaypoint(shortPath, longPath).value_or(CFixedVector2D{}), *cmpPathfinder,
					GetObstructionFilter(specificIgnore), m_Clearance, m_PassClass))
			{
				// Update pathfinding inertia based on actual movement
				state.inertiaVelocity = (pos - originalPos).Multiply(fixed::FromInt(1) / dt);

				// Skip waypoint popping for now - we'll handle it next turn
				return false;
			}
		}

		// On a path but pushed off it against something static, with no nudge to be had: slide along the obstacle
		// as a unit without a path does, by what the unit itself meant to move. Pressed by a crowd into the trees
		// at the edge of a chokepoint, it works its way along them instead of standing there asking for paths.
		if (hasPathfinding && !ownVelocity.IsZero() &&
			ObstructionResolution::TrySlide(pos, ownVelocity.Multiply(dt), GetEntityId(), *cmpPathfinder,
				GetObstructionFilter(specificIgnore), m_Clearance, m_PassClass))
			return false;

		// Units that weren't asked to move (e.g. idle units being pushed into a wall)
		// shouldn't trigger obstruction handling.
		return hasMoveRequest;
	}
}

void CCmpUnitMotion::UpdateMovementState(entity_pos_t speed, entity_pos_t meanSpeed)
{
	CmpPtr<ICmpVisual> cmpVisual(GetEntityHandle());
	if (cmpVisual)
	{
		// A crawl below the progress threshold is standing still as far as the eye is concerned. Braking
		// can leave a unit with a speed that is not quite zero, and walking on the spot for it is exactly
		// what makes a stuck unit look like it's still on its way somewhere.
		if (meanSpeed < MinProgressSpeed())
			cmpVisual->SelectMovementAnimation("idle", fixed::FromInt(1));
		else
			cmpVisual->SelectMovementAnimation(meanSpeed > (m_WalkSpeed / 2).Multiply(m_RunMultiplier + fixed::FromInt(1)) ? "run" : "walk", meanSpeed);
	}

	m_LastTurnSpeed = meanSpeed;
	m_CurrentSpeed = speed;
}

bool CCmpUnitMotion::HandleObstructedMove(bool moved)
{
	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (!cmpPosition || !cmpPosition->IsInWorld())
		return false;

	// We failed to move, inform other components as they might handle it.
	// (don't send messages on the first failure, as that would be too noisy).
	// Also don't increment above the initial MoveObstructed message if we actually manage to move a little.
	if (!moved || m_FailedMovements < 2)
	{
		if (!IncrementFailedMovementsAndMaybeNotify() && m_FailedMovements >= 2)
			MoveObstructed();
	}

	PathGoal goal;
	if (!ComputeGoal(goal, m_MoveRequest))
		return false;

	// At this point we have a position in the world since ComputeGoal checked for that.
	CFixedVector2D pos = cmpPosition->GetPosition2D();

	// Assume that we are merely obstructed and the long path is salvageable, so try going around the obstruction.
	// This could be a separate function, but it doesn't really make sense to call it outside of here, and I can't find a name.
	// I use an IIFE to have nice 'return' semantics still.
	if ([&]() -> bool {
		// If the goal is close enough, we should ignore any remaining long waypoint and just
		// short path there directly, as that improves behaviour in general - see D2095).
		if (InShortPathRange(goal, pos))
			return false;

		// On rare occasions, when following a short path, we can end up in a position where
		// the short pathfinder thinks we are inside an obstruction (and can leave)
		// but the CheckMovement logic doesn't. I believe the cause is a small numerical difference
		// in their calculation, but haven't been able to pinpoint it precisely.
		// In those cases, the solution is to back away to prevent the short-pathfinder from being confused.
		// TODO: this should only be done if we're obstructed by a static entity.
		if (!m_ShortPath.m_Waypoints.empty() && m_FailedMovements == BACKUP_HACK_DELAY)
		{
			Waypoint next = m_ShortPath.m_Waypoints.back();
			CFixedVector2D backUp(pos.X - next.x, pos.Y - next.z);
			backUp.Normalize();
			next.x = pos.X + backUp.X;
			next.z = pos.Y + backUp.Y;
			m_ShortPath.m_Waypoints.push_back(next);
			return true;
		}
		// Delete the next waypoint if it's reasonably close,
		// because it might be blocked by units and thus unreachable.
		// NB: this number is tricky. Make it too high, and units start going down dead ends, which looks odd (#5795)
		// Make it too low, and they might get stuck behind other obstructed entities.
		// It also has performance implications because it calls the short-pathfinder.
		fixed skipbeyond = std::max(ShortPathSearchRange() / 3, Pathfinding::NAVCELL_SIZE * 8);
		if (m_LongPath.m_Waypoints.size() > 1 &&
		    (pos - CFixedVector2D(m_LongPath.m_Waypoints.back().x, m_LongPath.m_Waypoints.back().z)).CompareLength(skipbeyond) < 0)
		{
			m_LongPath.m_Waypoints.pop_back();
		}
		else if (ShouldAlternatePathfinder())
		{
			// Recompute the whole thing occasionally, in case we got stuck in a dead end from removing long waypoints.
			RequestLongPath(pos, goal);
			return true;
		}

		if (m_LongPath.m_Waypoints.empty())
			return false;

		// Compute a short path in the general vicinity of the next waypoint, to help pathfinding in crowds.
		// The goal here is to manage to move in the general direction of our target, not to be super accurate.
		fixed radius = Clamp(skipbeyond/3, Pathfinding::NAVCELL_SIZE * 4, Pathfinding::NAVCELL_SIZE * 12);
		PathGoal subgoal = { PathGoal::CIRCLE, m_LongPath.m_Waypoints.back().x, m_LongPath.m_Waypoints.back().z, radius };
		RequestShortPath(pos, subgoal, false);
		return true;
	}()) return true;

	// If we couldn't use a workaround, try recomputing the entire path.
	ComputePathToGoal(pos, goal);

	return true;
}

bool CCmpUnitMotion::TargetHasValidPosition(const MoveRequest& moveRequest) const
{
	if (moveRequest.m_Type != MoveRequest::ENTITY)
		return true;

	CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), moveRequest.m_Entity);
	return cmpPosition && cmpPosition->IsInWorld();
}

bool CCmpUnitMotion::ComputeTargetPosition(CFixedVector2D& out, const MoveRequest& moveRequest) const
{
	if (moveRequest.m_Type == MoveRequest::POINT)
	{
		out = moveRequest.m_Position;
		return true;
	}

	CmpPtr<ICmpPosition> cmpTargetPosition(GetSimContext(), moveRequest.m_Entity);
	if (!cmpTargetPosition || !cmpTargetPosition->IsInWorld())
		return false;

	if (moveRequest.m_Type == MoveRequest::OFFSET)
	{
		// There is an offset, so compute it relative to orientation
		entity_angle_t angle = cmpTargetPosition->GetRotation().Y;
		CFixedVector2D offset = moveRequest.GetOffset().Rotate(angle);
		out = cmpTargetPosition->GetPosition2D() + offset;
	}
	else
	{
		out = cmpTargetPosition->GetPosition2D();
		// Position is only updated after all units have moved & pushed.
		// Therefore, we may need to interpolate the target position, depending on when this call takes place during the turn:
		//  - On "Turn Start", we'll check positions directly without interpolation.
		//  - During movement, we'll call this for direct-pathing & we need to interpolate
		//    (this way, we move where the unit will end up at the end of _this_ turn, making it match on next turn start).
		//  - After movement, we'll call this to request paths & we need to interpolate
		//    (this way, we'll move where the unit ends up in the end of _next_ turn, making it a match in 2 turns).
		// TODO: This does not really aim many turns in advance, with orthogonal trajectories it probably should.
		CmpPtr<ICmpUnitMotion> cmpUnitMotion(GetSimContext(), moveRequest.m_Entity);
		CmpPtr<ICmpUnitMotionManager> cmpUnitMotionManager(GetSystemEntity());
		bool needInterpolation = cmpUnitMotion && cmpUnitMotion->IsMoveRequested() && cmpUnitMotionManager->ComputingMotion();
		if (needInterpolation)
		{
			// Add predicted movement.
			CFixedVector2D tempPos = out + (out - cmpTargetPosition->GetPreviousPosition2D());
			out = tempPos;
		}
	}
	return true;
}

bool CCmpUnitMotion::TryGoingStraightToTarget(const CFixedVector2D& from, bool updatePaths)
{
	// Assume if we have short paths we want to follow them.
	// Exception: offset movement (formations) generally have very short deltas
	// and to look good we need them to walk-straight most of the time.
	if (!IsFormationMember() && !m_ShortPath.m_Waypoints.empty())
		return false;

	CFixedVector2D targetPos;
	if (!ComputeTargetPosition(targetPos))
		return false;

	CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
	if (!cmpPathfinder)
		return false;

	// Move the goal to match the target entity's new position
	PathGoal goal;
	if (!ComputeGoal(goal, m_MoveRequest))
		return false;
	goal.x = targetPos.X;
	goal.z = targetPos.Y;
	// (we ignore changes to the target's rotation, since only buildings are
	// square and buildings don't move)

	// Find the point on the goal shape that we should head towards
	CFixedVector2D goalPos = goal.NearestPointOnGoal(from);

	// Fail if the target is too far away
	if ((goalPos - from).CompareLength(DIRECT_PATH_RANGE) > 0)
		return false;

	// Check if there's any collisions on that route.
	// For entity goals, skip only the specific obstruction tag or with e.g. walls we might ignore too many entities.
	ICmpObstructionManager::tag_t specificIgnore;
	if (m_MoveRequest.m_Type == MoveRequest::ENTITY)
	{
		CmpPtr<ICmpObstruction> cmpTargetObstruction(GetSimContext(), m_MoveRequest.m_Entity);
		if (cmpTargetObstruction)
			specificIgnore = cmpTargetObstruction->GetObstruction();
	}

	// Check movement against units - we want to use the short pathfinder to walk around those if needed.
	if (specificIgnore.valid())
	{
		if (!cmpPathfinder->CheckMovement(GetObstructionFilter(specificIgnore), from.X, from.Y, goalPos.X, goalPos.Y, m_Clearance, m_PassClass))
			return false;
	}
	else if (!cmpPathfinder->CheckMovement(GetObstructionFilter(), from.X, from.Y, goalPos.X, goalPos.Y, m_Clearance, m_PassClass))
		return false;

	if (!updatePaths)
		return true;

	// That route is okay, so update our path
	m_LongPath.m_Waypoints.clear();
	m_ShortPath.m_Waypoints.clear();
	m_ShortPath.m_Waypoints.emplace_back(Waypoint{ goalPos.X, goalPos.Y });
	return true;
}

bool CCmpUnitMotion::PathingUpdateNeeded(const CFixedVector2D& from) const
{
	if (m_MoveRequest.m_Type == MoveRequest::NONE)
		return false;

	CFixedVector2D targetPos;
	if (!ComputeTargetPosition(targetPos))
		return false;

	if (m_FollowKnownImperfectPathCountdown > 0 && (!m_LongPath.m_Waypoints.empty() || !m_ShortPath.m_Waypoints.empty()))
		return false;

	if (PossiblyAtDestination())
		return false;

	// Get the obstruction shape and translate it where we estimate the target to be.
	ICmpObstructionManager::ObstructionSquare estimatedTargetShape;
	if (m_MoveRequest.m_Type == MoveRequest::ENTITY)
	{
		CmpPtr<ICmpObstruction> cmpTargetObstruction(GetSimContext(), m_MoveRequest.m_Entity);
		if (cmpTargetObstruction)
			cmpTargetObstruction->GetObstructionSquare(estimatedTargetShape);
	}

	estimatedTargetShape.x = targetPos.X;
	estimatedTargetShape.z = targetPos.Y;

	CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
	ICmpObstructionManager::ObstructionSquare shape;
	if (cmpObstruction)
		cmpObstruction->GetObstructionSquare(shape);

	// Translate our own obstruction shape to our last waypoint or our current position, lacking that.
	if (m_LongPath.m_Waypoints.empty() && m_ShortPath.m_Waypoints.empty())
	{
		shape.x = from.X;
		shape.z = from.Y;
	}
	else
	{
		const Waypoint& lastWaypoint = m_LongPath.m_Waypoints.empty() ? m_ShortPath.m_Waypoints.front() : m_LongPath.m_Waypoints.front();
		shape.x = lastWaypoint.x;
		shape.z = lastWaypoint.z;
	}

	CmpPtr<ICmpObstructionManager> cmpObstructionManager(GetSystemEntity());
	ENSURE(cmpObstructionManager);

	// Increase the ranges with distance, to avoid recomputing every turn against units that are moving and far-away for example.
	entity_pos_t distance = (from - CFixedVector2D(estimatedTargetShape.x, estimatedTargetShape.z)).Length();

	// TODO: it could be worth computing this based on time to collision instead of linear distance.
	entity_pos_t minRange = std::max(m_MoveRequest.m_MinRange - distance / TARGET_UNCERTAINTY_MULTIPLIER, entity_pos_t::Zero());
	entity_pos_t maxRange = m_MoveRequest.m_MaxRange < entity_pos_t::Zero() ? m_MoveRequest.m_MaxRange :
	    m_MoveRequest.m_MaxRange + distance / TARGET_UNCERTAINTY_MULTIPLIER;

	if (cmpObstructionManager->AreShapesInRange(shape, estimatedTargetShape, minRange, maxRange, false))
		return false;

	return true;
}

void CCmpUnitMotion::FaceTowardsPoint(entity_pos_t x, entity_pos_t z)
{
	// The motion system does the actual turning, every turn and with angular momentum (see FaceTowardsPointFromPos).
	// UnitAI calls this repeatedly while attacking, which just keeps the target heading current.
	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (!cmpPosition || !cmpPosition->IsInWorld())
		return;

	CFixedVector2D pos = cmpPosition->GetPosition2D();
	FaceTowardsPointFromPos(pos, x, z);
}

void CCmpUnitMotion::FaceTowardsPointFromPos(const CFixedVector2D& pos, entity_pos_t x, entity_pos_t z)
{
	const CFixedVector2D offset{CFixedVector2D(x, z) - pos};
	if (offset.IsZero())
		return;
	const entity_angle_t targetAngle{atan2_approx(offset.X, offset.Y)};

	CmpPtr<CCmpUnitMotionManager> cmpMotionMgr(GetSystemEntity());
	MotionState* state{cmpMotionMgr ? cmpMotionMgr->GetMotionState(GetEntityId()) : nullptr};
	if (!state)
	{
		// Not handled by the motion manager, so nothing would keep turning us over the following turns.
		CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
		if (cmpPosition && cmpPosition->IsInWorld())
			cmpPosition->TurnTo(targetAngle);
		return;
	}

	// While moving, the movement controller owns our heading.
	if (m_MoveRequest.m_Type != MoveRequest::NONE)
		return;

	// Don't turn here: set a heading that PerformMove turns towards every turn, with angular momentum and the max turn rate,
	// until aligned (PreMove keeps us awake meanwhile). Turning in one call could only ever take a single step.
	state->hasFaceTarget = true;
	state->faceTargetAngle = targetAngle;
}

// The pathfinder cannot go to "rounded rectangles" goals, which are what happens with square targets and a non-null range.
// Depending on what the best approximation is, we either pretend the target is a circle or a square.
// One needs to be careful that the approximated geometry will be in the range.
bool CCmpUnitMotion::ShouldTreatTargetAsCircle(entity_pos_t range, entity_pos_t circleRadius) const
{
	// Given a square, plus a target range we should reach, the shape at that distance
	// is a round-cornered square which we can approximate as either a circle or as a square.
	// Previously, we used the shape that minimized the worst-case error.
	// However that is unsage in some situations. So let's be less clever and
	// just check if our range is at least three times bigger than the circleradius
	return (range > circleRadius*3);
}

bool CCmpUnitMotion::ComputeGoal(PathGoal& out, const MoveRequest& moveRequest) const
{
	if (moveRequest.m_Type == MoveRequest::NONE)
		return false;

	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (!cmpPosition || !cmpPosition->IsInWorld())
		return false;

	CFixedVector2D pos = cmpPosition->GetPosition2D();

	CFixedVector2D targetPosition;
	if (!ComputeTargetPosition(targetPosition, moveRequest))
		return false;

	ICmpObstructionManager::ObstructionSquare targetObstruction;
	if (moveRequest.m_Type == MoveRequest::ENTITY)
	{
		CmpPtr<ICmpObstruction> cmpTargetObstruction(GetSimContext(), moveRequest.m_Entity);
		if (cmpTargetObstruction)
			cmpTargetObstruction->GetObstructionSquare(targetObstruction);
	}
	targetObstruction.x = targetPosition.X;
	targetObstruction.z = targetPosition.Y;

	ICmpObstructionManager::ObstructionSquare obstruction;
	CmpPtr<ICmpObstruction> cmpObstruction(GetEntityHandle());
	if (cmpObstruction)
		cmpObstruction->GetObstructionSquare(obstruction);
	else
	{
		obstruction.x = pos.X;
		obstruction.z = pos.Y;
	}

	CmpPtr<ICmpObstructionManager> cmpObstructionManager(GetSystemEntity());
	ENSURE(cmpObstructionManager);

	out.x = targetObstruction.x;
	out.z = targetObstruction.z;
	out.hw = targetObstruction.hw;
	out.hh = targetObstruction.hh;
	out.u = targetObstruction.u;
	out.v = targetObstruction.v;

	if (moveRequest.m_MinRange > fixed::Zero() || moveRequest.m_MaxRange > fixed::Zero() ||
	     targetObstruction.hw > fixed::Zero())
		out.type = PathGoal::SQUARE;
	else
	{
		out.type = PathGoal::POINT;
		return true;
	}

	entity_pos_t distance = cmpObstructionManager->DistanceBetweenShapes(obstruction, targetObstruction);

	entity_pos_t circleRadius = CFixedVector2D(targetObstruction.hw, targetObstruction.hh).Length();

	// TODO: because we cannot move to rounded rectangles, we have to make conservative approximations.
	// This means we might end up in a situation where cons(max-range) < min range < max range < cons(min-range)
	// When going outside of the min-range or inside the max-range, the unit will still go through the correct range
	// but if it moves fast enough, this might not be picked up by PossiblyAtDestination().
	// Fixing this involves moving to rounded rectangles, or checking more often in PerformMove().
	// In the meantime, one should avoid that 'Speed over a turn' > MaxRange - MinRange, in case where
	// min-range is not 0 and max-range is not infinity.
	if (distance < moveRequest.m_MinRange)
	{
		// Distance checks are nearest edge to nearest edge, so we need to account for our clearance
		// and we must make sure diagonals also fit so multiply by slightly more than sqrt(2)
		entity_pos_t goalDistance = moveRequest.m_MinRange + m_Clearance * 3 / 2;

		if (ShouldTreatTargetAsCircle(moveRequest.m_MinRange, circleRadius))
		{
			// We are safely away from the obstruction itself if we are away from the circumscribing circle
			out.type = PathGoal::INVERTED_CIRCLE;
			out.hw = circleRadius + goalDistance;
		}
		else
		{
			out.type = PathGoal::INVERTED_SQUARE;
			out.hw = targetObstruction.hw + goalDistance;
			out.hh = targetObstruction.hh + goalDistance;
		}
	}
	else if (moveRequest.m_MaxRange >= fixed::Zero() && distance > moveRequest.m_MaxRange)
	{
		if (ShouldTreatTargetAsCircle(moveRequest.m_MaxRange, circleRadius))
		{
			entity_pos_t goalDistance = moveRequest.m_MaxRange;
			// We must go in-range of the inscribed circle, not the circumscribing circle.
			circleRadius = std::min(targetObstruction.hw, targetObstruction.hh);

			out.type = PathGoal::CIRCLE;
			out.hw = circleRadius + goalDistance;
		}
		else
		{
			// The target is large relative to our range, so treat it as a square and
			// get close enough that the diagonals come within range

			entity_pos_t goalDistance = moveRequest.m_MaxRange * 2 / 3; // multiply by slightly less than 1/sqrt(2)

			out.type = PathGoal::SQUARE;
			entity_pos_t delta = std::max(goalDistance, m_Clearance + entity_pos_t::FromInt(4)/16); // ensure it's far enough to not intersect the building itself
			out.hw = targetObstruction.hw + delta;
			out.hh = targetObstruction.hh + delta;
		}
	}
	// Do nothing in particular in case we are already in range.
	return true;
}

void CCmpUnitMotion::ComputePathToGoal(const CFixedVector2D& from, const PathGoal& goal)
{
#if DISABLE_PATHFINDER
	{
		CmpPtr<ICmpPathfinder> cmpPathfinder (GetSimContext(), SYSTEM_ENTITY);
		CFixedVector2D goalPos = m_FinalGoal.NearestPointOnGoal(from);
		m_LongPath.m_Waypoints.clear();
		m_ShortPath.m_Waypoints.clear();
		m_ShortPath.m_Waypoints.emplace_back(Waypoint{ goalPos.X, goalPos.Y });
		return;
	}
#endif

	// If the target is close enough, hope that we'll be able to go straight next turn.
	if (!ShouldAlternatePathfinder() && TryGoingStraightToTarget(from, false))
	{
		// NB: since we may fail to move straight next turn, we should edge our bets.
		// Since the 'go straight' logic currently fires only if there's no short path,
		// we'll compute a long path regardless to make sure _that_ stays up to date.
		// (it's also extremely likely to be very fast to compute, so no big deal).
		m_ShortPath.m_Waypoints.clear();
		RequestLongPath(from, goal);
		return;
	}

	// Otherwise we need to compute a path.

	// If it's close then just do a short path, not a long path
	// TODO: If it's close on the opposite side of a river then we really
	// need a long path, so we shouldn't simply check linear distance
	// the check is arbitrary but should be a reasonably small distance.
	// We want to occasionally compute a long path if we're computing short-paths, because the short path domain
	// is bounded and thus it can't around very large static obstacles.
	// Likewise, we want to compile a short-path occasionally when the target is far because we might be stuck
	// on a navcell surrounded by impassable navcells, but the short-pathfinder could move us out of there.
	bool shortPath = InShortPathRange(goal, from);
	if (ShouldAlternatePathfinder())
		shortPath = !shortPath;
	if (shortPath)
	{
		m_LongPath.m_Waypoints.clear();
		// Extend the range so that our first path is probably valid.
		RequestShortPath(from, goal, true);
	}
	else
	{
		m_ShortPath.m_Waypoints.clear();
		RequestLongPath(from, goal);
	}
}

void CCmpUnitMotion::RequestLongPath(const CFixedVector2D& from, const PathGoal& goal)
{
	CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
	if (!cmpPathfinder)
		return;

	// this is by how much our waypoints will be apart at most.
	// this value here seems sensible enough.
	PathGoal improvedGoal = goal;
	improvedGoal.maxdist = SHORT_PATH_MIN_SEARCH_RANGE - entity_pos_t::FromInt(1);

	cmpPathfinder->SetDebugPath(from.X, from.Y, improvedGoal, m_PassClass);

	m_ExpectedPathTicket.m_Type = Ticket::LONG_PATH;
	m_ExpectedPathTicket.m_Ticket = cmpPathfinder->ComputePathAsync(from.X, from.Y, improvedGoal, m_PassClass, GetEntityId());
}

void CCmpUnitMotion::RequestShortPath(const CFixedVector2D &from, const PathGoal& goal, bool extendRange)
{
	CmpPtr<ICmpPathfinder> cmpPathfinder(GetSystemEntity());
	if (!cmpPathfinder)
		return;

	entity_pos_t searchRange = ShortPathSearchRange();
	if (extendRange)
	{
		CFixedVector2D dist(from.X - goal.x, from.Y - goal.z);
		if (dist.CompareLength(searchRange - entity_pos_t::FromInt(1)) >= 0)
		{
			searchRange = dist.Length() + fixed::FromInt(1);
			if (searchRange > SHORT_PATH_MAX_SEARCH_RANGE)
				searchRange = SHORT_PATH_MAX_SEARCH_RANGE;
		}
	}

	m_ExpectedPathTicket.m_Type = Ticket::SHORT_PATH;
	m_ExpectedPathTicket.m_Ticket = cmpPathfinder->ComputeShortPathAsync(from.X, from.Y, m_Clearance, searchRange, goal, m_PassClass, ShouldCollideWithMovingUnits(), GetGroup(), GetEntityId());
}

bool CCmpUnitMotion::MoveTo(MoveRequest request)
{
	PROFILE("MoveTo");

	if (request.m_MinRange == request.m_MaxRange && !request.m_MinRange.IsZero())
		LOGWARNING("MaxRange must be larger than MinRange; See CCmpUnitMotion.cpp for more information");

	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (!cmpPosition || !cmpPosition->IsInWorld())
		return false;

	PathGoal goal;
	if (!ComputeGoal(goal, request))
		return false;

	m_MoveRequest = request;
	m_FailedMovements = 0;
	m_FollowKnownImperfectPathCountdown = 0;

	// Movement owns our heading from now on: drop any heading we were turning towards.
	{
		CmpPtr<CCmpUnitMotionManager> cmpMotionMgr(GetSystemEntity());
		if (MotionState* state{cmpMotionMgr ? cmpMotionMgr->GetMotionState(GetEntityId()) : nullptr})
			state->hasFaceTarget = false;
	}

	ComputePathToGoal(cmpPosition->GetPosition2D(), goal);
	return true;
}

bool CCmpUnitMotion::IsTargetRangeReachable(entity_id_t target, entity_pos_t minRange, entity_pos_t maxRange)
{
	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (!cmpPosition || !cmpPosition->IsInWorld())
		return false;

	MoveRequest request(target, minRange, maxRange);
	PathGoal goal;
	if (!ComputeGoal(goal, request))
		return false;

	CmpPtr<ICmpPathfinder> cmpPathfinder(GetSimContext(), SYSTEM_ENTITY);
	CFixedVector2D pos = cmpPosition->GetPosition2D();
	return cmpPathfinder->IsGoalReachable(pos.X, pos.Y, goal, m_PassClass);
}

void CCmpUnitMotion::RenderPath(const WaypointPath& path, std::vector<SOverlayLine>& lines, CColor color)
{
	bool floating = false;
	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (cmpPosition)
		floating = cmpPosition->CanFloat();

	lines.clear();
	std::vector<float> waypointCoords;
	for (size_t i = 0; i < path.m_Waypoints.size(); ++i)
	{
		float x = path.m_Waypoints[i].x.ToFloat();
		float z = path.m_Waypoints[i].z.ToFloat();
		waypointCoords.push_back(x);
		waypointCoords.push_back(z);
		lines.push_back(SOverlayLine());
		lines.back().m_Color = color;
		SimRender::ConstructSquareOnGround(GetSimContext(), x, z, 1.0f, 1.0f, 0.0f, lines.back(), floating);
	}
	float x = cmpPosition->GetPosition2D().X.ToFloat();
	float z = cmpPosition->GetPosition2D().Y.ToFloat();
	waypointCoords.push_back(x);
	waypointCoords.push_back(z);
	lines.push_back(SOverlayLine());
	lines.back().m_Color = color;
	SimRender::ConstructLineOnGround(GetSimContext(), waypointCoords, lines.back(), floating);

}

void CCmpUnitMotion::RenderSubmit(SceneCollector& collector)
{
    if (!m_DebugOverlayEnabled)
        return;

    const auto& palette{m_IsFormationController ?
        FORMATION_CONTROLLER_PALETTE : REGULAR_UNIT_PALETTE};

    RenderPath(m_LongPath, m_DebugOverlayLongPathLines, palette.longPath);
    RenderPath(m_ShortPath, m_DebugOverlayShortPathLines, palette.shortPath);

	for (SOverlayLine& line : m_DebugOverlayLongPathLines)
		collector.Submit(&line);

	for (SOverlayLine& line : m_DebugOverlayShortPathLines)
		collector.Submit(&line);
}

#endif // INCLUDED_CCMPUNITMOTION
