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

#ifndef INCLUDED_PATHFOLLOWING
#define INCLUDED_PATHFOLLOWING

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/helpers/Position.h"

#include <optional>

/**
 * Following a unit's path: which waypoint it's heading for, and the velocity it steers at to get there.
 * A unit has a long path and a short path; both store their waypoints in reverse, so the current one is at the back,
 * and the short path (when there is one) leads to the long path's current waypoint.
 * Stateless: paths and the unit's movement are passed in.
 */
class PathFollowing
{
public:
	/**
	 * The waypoint the unit is heading for: the short path's current one, or else the long path's.
	 */
	static std::optional<CFixedVector2D> CurrentWaypoint(const WaypointPath& shortPath, const WaypointPath& longPath)
	{
		if (!shortPath.m_Waypoints.empty())
			return CFixedVector2D{shortPath.m_Waypoints.back().x, shortPath.m_Waypoints.back().z};
		if (!longPath.m_Waypoints.empty())
			return CFixedVector2D{longPath.m_Waypoints.back().x, longPath.m_Waypoints.back().z};
		return std::nullopt;
	}

	/**
	 * The waypoint after the current one, if any.
	 */
	static std::optional<CFixedVector2D> NextWaypoint(const WaypointPath& shortPath, const WaypointPath& longPath);

	/**
	 * The velocity a unit steers at towards its waypoint, before blending with its inertia.
	 * It heads straight for the waypoint at max speed, except that:
	 * - the sideways part (relative to its facing) shrinks as the unit speeds up, the more so the higher its inertia,
	 *   so a hull at speed carves an arc instead of sliding sideways;
	 * - it only pushes forwards, with at least (1 - turnBraking) of full throttle, so it keeps carving while turning,
	 *   unless it's avoiding a predicted collision.
	 * @param toWaypoint - vector from the unit to its waypoint; if zero, the unit doesn't steer.
	 * @param speed - the unit's current speed.
	 */
	static CFixedVector2D SteeringVelocity(CFixedVector2D toWaypoint, const CFixedVector2D& facingDir, const fixed speed,
		const fixed maxSpeed, const fixed inertiaStrength, const fixed turnBraking, const bool avoidingCollision);

	/**
	 * Where the path ends: the long path's last waypoint, or else the short path's. That is the goal point for a
	 * plain point order, and otherwise the nearest reachable point in range of the goal.
	 */
	static std::optional<CFixedVector2D> FinalWaypoint(const WaypointPath& shortPath, const WaypointPath& longPath)
	{
		if (!longPath.m_Waypoints.empty())
			return CFixedVector2D{longPath.m_Waypoints.front().x, longPath.m_Waypoints.front().z};
		if (!shortPath.m_Waypoints.empty())
			return CFixedVector2D{shortPath.m_Waypoints.front().x, shortPath.m_Waypoints.front().z};
		return std::nullopt;
	}

	/**
	 * How far a unit coasts before its inertia lets it stop: speed·dt·(1 + inertia), the sum of the exponential decay.
	 */
	static fixed StoppingDistance(const fixed speed, const fixed dt, const fixed inertiaStrength)
	{
		return speed.Multiply(dt).Multiply(fixed::FromInt(1) + inertiaStrength);
	}

	/**
	 * How well two directions line up: 1 = same direction, 0 = at right angles or pointing apart.
	 * Both vectors must be non-zero.
	 */
	static fixed Alignment(CFixedVector2D direction, CFixedVector2D other);

	/**
	 * How well two directions line up (1 = same direction, 0 = at right angles or pointing apart), for callers
	 * that already know both lengths: the dot product over their product. Needs no square root, and is the more
	 * accurate of the two overloads: normalizing first rounds both vectors down to unit length, discarding the
	 * bits above the point before the dot product ever sees them.
	 * Lengths must match their vectors, and both must be non-zero.
	 */
	static fixed Alignment(const CFixedVector2D& direction, const fixed length,
		const CFixedVector2D& other, const fixed otherLength);

	/**
	 * The speed a unit may keep while turning by turnAngle (radians): turnBraking is the share of top speed given up
	 * at a right angle or sharper (0 = never slow down, 1 = stop and turn in place), 1 - Cornering in the template.
	 * It's a cap rather than a per-turn multiplier, so the loss doesn't compound with how long the turn takes.
	 * @return nullopt if the unit doesn't slow down for this turn.
	 */
	static std::optional<fixed> TurnSpeedCap(const entity_angle_t turnAngle, const fixed turnBraking, const fixed maxSpeed);

	/**
	 * Speed multiplier for a unit within braking distance of its waypoint. It brakes for misalignment only, with
	 * the waypoint or with a corner it can't cut: heading straight on, it keeps its speed; the worse aligned and
	 * the closer, the more it brakes, down to its alignment's share of its speed.
	 * It never brakes below the speed that still closes the distance within the braking window, so a unit braking
	 * for a corner doesn't stop short of it.
	 * @param alignment - 1 = heading straight on, 0 = at right angles or worse.
	 * Only valid within the braking distance, at a non-zero speed. The result is between 0 and 1.
	 */
	static fixed ProximityBrakeScale(const fixed distance, const fixed brakingDistance, const fixed speed, const fixed alignment);

	/**
	 * Speed multiplier to stop at the end of the path, braking at the constant deceleration that stops the unit
	 * from @p maxSpeed over @p maxStoppingDistance, and never going past the end within @p dt.
	 * @return nullopt if @p speed needs no braking yet.
	 */
	static std::optional<fixed> PredictiveBrakeFactor(const fixed distanceToEnd, const fixed speed,
		const fixed maxSpeed, const fixed maxStoppingDistance, const fixed dt);

	/**
	 * How close a unit must get to its waypoint to count it as reached.
	 * Based on the speed momentum carries (unbrakedSpeed: braking for this very waypoint mustn't shrink it),
	 * between a navcell and the stopping distance at max speed, then:
	 * - smaller when heading straight for the waypoint, so corners aren't cut;
	 * - larger once the unit is closer to the next waypoint than to this one, so inertia carrying it past is fine;
	 * - smaller with more drift correction, as the unit steers more precisely.
	 * @param velocity - the velocity actually moved at this turn (after braking).
	 * @param speed - velocity's length, and @param distanceToWaypoint the distance from pos to waypoint. Both are
	 *	passed in rather than recomputed: the caller already holds them, and each would otherwise cost a square root.
	 */
	static fixed AcceptanceRadius(const CFixedVector2D& pos, const CFixedVector2D& waypoint,
		const std::optional<CFixedVector2D>& nextWaypoint, const CFixedVector2D& velocity, const fixed speed,
		const fixed distanceToWaypoint, const fixed unbrakedSpeed,
		const fixed maxSpeed, const fixed inertiaStrength, const fixed driftCorrection, const fixed dt);

	/**
	 * Whether the unit is already beyond @p waypoint on the way to @p nextWaypoint: pushed or carried past it, it
	 * would otherwise turn round and go back for it, against the flow of whatever pushed it. The caller still checks
	 * the way on to the next waypoint is clear, so a waypoint at a corner round an obstacle is kept.
	 */
	static bool PassedWaypoint(const CFixedVector2D& pos, const CFixedVector2D& waypoint, const CFixedVector2D& nextWaypoint)
	{
		return (pos - waypoint).Dot(nextWaypoint - waypoint) > fixed::Zero();
	}

	/**
	 * Whether the waypoint is inside the unit's turning circle, so it can't be reached without circling back.
	 * The circle is drawn with the turn rate the unit can reach over the angle left, not its top rate: a unit that
	 * turns up slowly has a much wider circle than its top rate says.
	 * Only asked within maxDistance (the stopping distance), which also keeps distance² small.
	 * @param speed - velocity's length, and @param distance the distance from pos to waypoint. Passed in for the
	 *	same reason as in AcceptanceRadius: the caller has them, and recomputing costs a square root each.
	 */
	static bool CannotReach(const CFixedVector2D& pos, const CFixedVector2D& waypoint, const CFixedVector2D& velocity,
		const fixed speed, const fixed distance, const entity_angle_t angle, const fixed turnRate,
		const fixed turnAcceleration, const fixed maxDistance);
};

#endif // INCLUDED_PATHFOLLOWING
