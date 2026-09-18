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

#ifndef INCLUDED_HEADINGCONTROL
#define INCLUDED_HEADINGCONTROL

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/helpers/Position.h"

/**
 * Heading maths for unit motion: angle wrapping, choosing the heading to face while following a path,
 * and turning towards a heading with angular momentum.
 * Stateless: everything a unit carries between turns (its angle and angular velocity) is passed in.
 */
class HeadingControl
{
public:
	/**
	 * The unit vector a unit at this heading faces.
	 * It costs a sine and a cosine, so anything needing it more than once in a turn should compute it once
	 * and pass it around rather than recompute it from the angle.
	 */
	static CFixedVector2D FacingDirection(const entity_angle_t angle)
	{
		return CFixedVector2D(fixed::Zero(), fixed::FromInt(1)).Rotate(angle);
	}

	/**
	 * Bring an angle back into [-PI, PI] with a single correction of 2·PI.
	 * Only valid for angles within (-3·PI, 3·PI), e.g. the difference of two normalized angles.
	 */
	static entity_angle_t WrapAngle(const entity_angle_t angle)
	{
		if (angle > entity_angle_t::Pi())
			return angle - entity_angle_t::Pi() * 2;
		if (angle < -entity_angle_t::Pi())
			return angle + entity_angle_t::Pi() * 2;
		return angle;
	}

	/**
	 * Bring any angle back into [-PI, PI], however far outside it is.
	 */
	static entity_angle_t NormalizeAngle(entity_angle_t angle)
	{
		while (angle > entity_angle_t::Pi())
			angle -= entity_angle_t::Pi() * 2;
		while (angle < -entity_angle_t::Pi())
			angle += entity_angle_t::Pi() * 2;
		return angle;
	}

	/**
	 * The heading to face while following a path: the direction to the waypoint, pulled towards the direction
	 * of travel by movementWeight (0 = face the waypoint, 1 = face where we're going).
	 * @param waypointAngle - the heading from the unit to its current waypoint. Taken as an angle because the caller
	 *	already needs it for its own turn, and converting the vector again would cost a second atan2.
	 * @param facingVelocity - the velocity the unit means to travel at.
	 */
	static entity_angle_t PathFacingAngle(const entity_angle_t waypointAngle, const CFixedVector2D& facingVelocity,
		const fixed movementWeight);

	/**
	 * Turn one step towards a target heading with angular momentum. Every kind of turn uses this: following a path,
	 * facing the move target before its path arrives, and facing an attack target or a destination.
	 * Minimum-time turn: the fastest angular speed from which we can still stop on the target, sqrt(2·accel·|angle|), capped
	 * at maxRate. The signed angular velocity moves toward it by at most accel·dt, which speeds the turn up, slows it down
	 * before the target and handles reversing. Snaps onto the target only while rotating towards it.
	 * @return true once aligned (angle set to the target and angular velocity zeroed).
	 */
	static bool TurnWithMomentum(entity_angle_t& angle, fixed& angularVelocity, entity_angle_t targetAngle,
		const fixed maxRate, const fixed accel, const fixed dt);
};

#endif // INCLUDED_HEADINGCONTROL
