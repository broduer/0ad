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
#include "PathFollowing.h"

#include "maths/MathUtil.h"
#include "simulation2/helpers/HeadingControl.h"
#include "simulation2/helpers/MomentumModel.h"

#include <algorithm>

namespace
{
/**
 * Maximum reduction of acceptance radius when perfectly aligned with waypoint.
 * Higher values = more aggressive reduction = less corner cutting.
 */
constexpr fixed WAYPOINT_ALIGNMENT_REDUCTION{fixed::FromFraction(4, 10)};

/**
 * Minimum acceptance radius scale when aligned with waypoint.
 * Prevents radius from becoming too small and causing units to miss waypoints.
 */
constexpr fixed WAYPOINT_ALIGNMENT_MIN_SCALE{fixed::FromFraction(2, 10)};

/**
 * The slowest proximity braking takes a unit to (about 1.5 mm/s): never a full stop.
 */
constexpr fixed MIN_BRAKED_SPEED{fixed::FromFraction(100, 1 << 16)}; // 100 raw units
} // anonymous namespace

std::optional<CFixedVector2D> PathFollowing::NextWaypoint(const WaypointPath& shortPath, const WaypointPath& longPath)
{
	if (shortPath.m_Waypoints.size() > 1)
	{
		const Waypoint& next{shortPath.m_Waypoints[shortPath.m_Waypoints.size() - 2]};
		return CFixedVector2D{next.x, next.z};
	}
	if (shortPath.m_Waypoints.size() == 1 && !longPath.m_Waypoints.empty())
	{
		const Waypoint& next{longPath.m_Waypoints.back()};
		return CFixedVector2D{next.x, next.z};
	}
	if (shortPath.m_Waypoints.empty() && longPath.m_Waypoints.size() > 1)
	{
		const Waypoint& next{longPath.m_Waypoints[longPath.m_Waypoints.size() - 2]};
		return CFixedVector2D{next.x, next.z};
	}
	return std::nullopt;
}

CFixedVector2D PathFollowing::SteeringVelocity(CFixedVector2D toWaypoint, const CFixedVector2D& facingDir, const fixed speed,
	const fixed maxSpeed, const fixed inertiaStrength, const fixed turnBraking, const bool avoidingCollision)
{
	if (toWaypoint.IsZero())
		return CFixedVector2D{};

	toWaypoint.Normalize(maxSpeed);

	const CFixedVector2D rightDir{facingDir.Perpendicular()};

	const fixed forwardComponent{toWaypoint.Dot(facingDir)};
	const fixed lateralComponent{toWaypoint.Dot(rightDir)};

	// How much sideways movement to allow: all of it when standing (a standing unit can be nudged any way),
	// down to 1 / (1 + inertia) at max speed (with inertia 10, only ~10%).
	const fixed speedRatio{maxSpeed > fixed::Epsilon() ? speed / maxSpeed : fixed::FromInt(1)};
	const fixed minLateralScale{inertiaStrength > fixed::Epsilon() ?
		fixed::FromInt(1) / (fixed::FromInt(1) + inertiaStrength) : fixed::FromInt(1)};
	const fixed lateralScale{fixed::FromInt(1) - speedRatio.Multiply(fixed::FromInt(1) - minLateralScale)};

	// Units propel themselves forward: they can't push backward. A waypoint behind us is reached by turning
	// rather than by reversing in place. They also keep some throttle on while turning, so they carve instead of
	// coasting to a stop. Cornering says how much of a base speed stays on; the base is the current speed, plus
	// inertia / (1 + inertia) of what is missing to max speed, so that a stalled high-inertia unit still carves
	// where a low-inertia one pivots.
	const fixed momentumShare{inertiaStrength > fixed::Epsilon() ?
		inertiaStrength / (fixed::FromInt(1) + inertiaStrength) : fixed::Zero()};
	const fixed floorSpeed{speed + (maxSpeed - speed).Multiply(momentumShare)};

	// ... But not while we're avoiding something: a unit facing an obstacle should turn away rather
	// than keep steaming into it.
	const fixed thrustFloor{avoidingCollision ? fixed::Zero() :
		std::max(floorSpeed.Multiply(fixed::FromInt(1) - turnBraking), fixed::Zero())};
	const fixed thrustForward{std::max(forwardComponent, thrustFloor)};

	return MomentumModel::ClampSpeed(facingDir.Multiply(thrustForward) + rightDir.Multiply(lateralComponent.Multiply(lateralScale)),
		maxSpeed);
}

fixed PathFollowing::Alignment(CFixedVector2D direction, CFixedVector2D other)
{
	direction.Normalize();
	other.Normalize();
	return Clamp(direction.Dot(other), fixed::Zero(), fixed::FromInt(1));
}

fixed PathFollowing::Alignment(const CFixedVector2D& direction, const fixed length,
	const CFixedVector2D& other, const fixed otherLength)
{
	const fixed lengths{length.Multiply(otherLength)};
	if (lengths <= fixed::Zero())
		return fixed::Zero();
	return Clamp(direction.Dot(other) / lengths, fixed::Zero(), fixed::FromInt(1));
}

std::optional<fixed> PathFollowing::TurnSpeedCap(const entity_angle_t turnAngle, const fixed turnBraking, const fixed maxSpeed)
{
	if (turnAngle <= fixed::Epsilon() || turnBraking <= fixed::Zero())
		return std::nullopt;

	// Not scaled by inertia: inertia already keeps us on a wide arc through the momentum blend,
	// and scaling the brake by it would make high-inertia units stall and pivot instead.
	const fixed turnSharpness{std::min(turnAngle / (entity_angle_t::Pi() / 2), fixed::FromInt(1))};
	const fixed brakingFactor{std::min(turnSharpness.Multiply(turnBraking), fixed::FromInt(1))};
	return maxSpeed.Multiply(fixed::FromInt(1) - brakingFactor);
}

fixed PathFollowing::ProximityBrakeScale(const fixed distance, const fixed brakingDistance, const fixed speed, const fixed alignment)
{
	// Ease in: barely brake at the edge of the braking distance, fully at the waypoint.
	const fixed proximityFactor{Clamp(distance / brakingDistance, fixed::Zero(), fixed::FromInt(1))};
	const fixed brakeStrength{fixed::FromInt(1) - proximityFactor.Multiply(proximityFactor)};

	// Poor alignment or a sharp corner brakes up to twice as hard.
	const fixed totalBrake{Clamp(brakeStrength.Multiply(fixed::FromInt(1) + (fixed::FromInt(1) - alignment)),
		fixed::Zero(), fixed::FromInt(1))};

	// The least share of its speed a unit keeps, however hard it brakes. The largest of:
	// - its alignment: the better aimed, the more speed it keeps;
	// - enough to still reach the waypoint, so braking for a sharp corner never stops a unit short of it. The
	//   square root makes it a constant deceleration, which gets there rather than creeping towards it;
	// - a crawl, MIN_BRAKED_SPEED, since a unit that brakes to zero reports its move as obstructed.
	// Capped at 1, so braking never speeds up a unit already slower than the crawl.
	const fixed minSpeedScale{std::min(std::max(std::max(alignment, proximityFactor.Sqrt()), MIN_BRAKED_SPEED / speed),
		fixed::FromInt(1))};

	// The scale approaches minSpeedScale as braking strength increases.
	return fixed::FromInt(1) - totalBrake.Multiply(fixed::FromInt(1) - minSpeedScale);
}

std::optional<fixed> PathFollowing::PredictiveBrakeFactor(const fixed distanceToEnd, const fixed speed,
	const fixed maxSpeed, const fixed maxStoppingDistance, const fixed dt)
{
	if (speed <= fixed::Epsilon() || maxStoppingDistance <= fixed::Epsilon() || distanceToEnd >= maxStoppingDistance)
		return std::nullopt;

	// Brake at a constant deceleration, the speed left is : maxSpeed * sqrt(distanceToEnd / maxStoppingDistance).
	fixed cap{maxSpeed.Multiply((distanceToEnd / maxStoppingDistance).Sqrt())};
	// Never past the end within this turn.
	if (dt > fixed::Zero())
		cap = std::min(cap, distanceToEnd / dt);
	// A unit that brakes to zero reports its move as obstructed.
	cap = std::max(cap, MIN_BRAKED_SPEED);

	if (speed <= cap)
		return std::nullopt;
	return cap / speed;
}

fixed PathFollowing::AcceptanceRadius(const CFixedVector2D& pos, const CFixedVector2D& waypoint,
	const std::optional<CFixedVector2D>& nextWaypoint, const CFixedVector2D& velocity, const fixed speed,
	const fixed distanceToWaypoint, const fixed unbrakedSpeed,
	const fixed maxSpeed, const fixed inertiaStrength, const fixed driftCorrection, const fixed dt)
{
	// At least a navcell, so a slow unit doesn't get stuck short of its waypoint, and at most the stopping distance
	// at max speed, so a fast one doesn't pop waypoints too early.
	const fixed radius{std::min(std::max(StoppingDistance(unbrakedSpeed, dt, inertiaStrength), Pathfinding::NAVCELL_SIZE),
		StoppingDistance(maxSpeed, dt, inertiaStrength))};

	const CFixedVector2D toWaypoint{waypoint - pos};
	if (speed <= fixed::Epsilon() || distanceToWaypoint <= fixed::Epsilon())
		return radius;

	// Heading straight for the waypoint: smaller, so corners aren't cut. Sideways or away: keep it.
	// Both lengths are already in hand, so this costs no square root.
	const fixed alignment{Alignment(velocity, speed, toWaypoint, distanceToWaypoint)};
	fixed scale{std::max(fixed::FromInt(1) - alignment.Multiply(WAYPOINT_ALIGNMENT_REDUCTION), WAYPOINT_ALIGNMENT_MIN_SCALE)};

	// Closer to the next waypoint than to this one: larger, up to 1 + (1 - WAYPOINT_ALIGNMENT_MIN_SCALE) at the next
	// waypoint, as inertia carried us past this one.
	if (nextWaypoint)
	{
		const fixed distanceToNextWaypoint{(*nextWaypoint - pos).Length()};
		if (distanceToNextWaypoint < distanceToWaypoint && (*nextWaypoint - waypoint).Length() > fixed::Epsilon())
		{
			const fixed progressToNext{fixed::FromInt(1) - distanceToNextWaypoint / distanceToWaypoint};
			scale = scale.Multiply(fixed::FromInt(1) + progressToNext.Multiply(fixed::FromInt(1) - WAYPOINT_ALIGNMENT_MIN_SCALE));
		}
	}

	// More drift correction: smaller, as the unit steers more precisely.
	if (driftCorrection > fixed::Zero())
		scale = scale.Multiply(std::max(fixed::FromInt(1) - driftCorrection.Multiply(WAYPOINT_ALIGNMENT_REDUCTION), WAYPOINT_ALIGNMENT_MIN_SCALE));

	return radius.Multiply(scale);
}

bool PathFollowing::CannotReach(const CFixedVector2D& pos, const CFixedVector2D& waypoint, const CFixedVector2D& velocity,
	const fixed speed, const fixed distance, const entity_angle_t angle, const fixed turnRate,
	const fixed turnAcceleration, const fixed maxDistance)
{
	const CFixedVector2D toWaypoint{waypoint - pos};
	if (speed <= fixed::Epsilon() || distance <= fixed::Epsilon() || distance >= maxDistance)
		return false;

	const entity_angle_t neededTurn{HeadingControl::WrapAngle(atan2_approx(toWaypoint.X, toWaypoint.Y) - angle)};
	const fixed achievableTurnRate{std::min(turnRate, (turnAcceleration.Multiply(neededTurn.Absolute()) * 2).Sqrt())};

	// The waypoint is inside the turning circle of radius r = speed / turn rate when
	//     forward² + (lateral - r)² < r²,  i.e.  dist² < 2·r·lateral
	// r divides by the rate, which can be zero (then we can't steer at all, so can't reach anything off our nose).
	// Both sides times the rate instead:
	//     dist² · rate < 2 · speed · lateral
	// The speed cancels: the lateral offset is |cross(toWaypoint, velocity)| / speed, and the right-hand side
	// multiplies it straight back by that speed - so neither the division nor the square root behind normalizing
	// the velocity is needed. dist² likewise comes from the vector rather than from squaring a length that a
	// square root has already rounded down.
	const fixed lateral{(toWaypoint.X.Multiply(velocity.Y) - toWaypoint.Y.Multiply(velocity.X)).Absolute()};
	return toWaypoint.Dot(toWaypoint).Multiply(achievableTurnRate) < lateral * 2;
}
