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

#ifndef INCLUDED_MOMENTUMMODEL
#define INCLUDED_MOMENTUMMODEL

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/helpers/Position.h"

#include <algorithm>

/**
 * Momentum maths for unit motion: how the velocity a unit wants (from its path) combines with the velocity it
 * already has (its inertia, and what pushes and impacts gave it), and what that costs.
 * Stateless: the velocities a unit carries between turns are passed in and returned.
 */
class MomentumModel
{
public:
	/**
	 * Speeds below this (m/s) are noise: fixed-point rounding rather than movement.
	 */
	static constexpr fixed NEGLIGIBLE_SPEED{fixed::FromFraction(1, 100)};

	/**
	 * A coasting unit whose remaining glide is shorter than this (metres) stops outright: it isn't visible.
	 */
	static constexpr fixed COASTING_REST_DISTANCE{fixed::FromFraction(1, 10)};

	/**
	 * Scale a velocity down to maxSpeed if it's faster. Slower velocities are returned unchanged.
	 */
	static CFixedVector2D ClampSpeed(const CFixedVector2D& velocity, const fixed maxSpeed)
	{
		// Most velocities reaching here are already within the cap, and the square root below would only be
		// used to decide that. Comparing squares costs a couple of multiplications instead.
		// Exact: length² <= maxSpeed² gives sqrt(length²) <= maxSpeed, and Length() floors that root, so a
		// velocity rejected here could never have satisfied length > maxSpeed.
		if (velocity.CompareLength(maxSpeed) <= 0)
			return velocity;

		const fixed length{velocity.Length()};
		if (length > maxSpeed && length > fixed::Epsilon())
			return velocity.Multiply(maxSpeed / length);
		return velocity;
	}

	/**
	 * Weighted average of the path velocity and the inertia velocity: (path + inertia·strength) / (1 + strength).
	 * Inertia carries speed as well as direction, so a unit at rest takes several turns to reach full speed.
	 */
	static CFixedVector2D BlendWithInertia(const CFixedVector2D& pathVelocity, const CFixedVector2D& inertiaVelocity,
		const fixed inertiaStrength)
	{
		return (pathVelocity + inertiaVelocity.Multiply(inertiaStrength)).Multiply(fixed::FromInt(1) / (fixed::FromInt(1) + inertiaStrength));
	}

	/**
	 * Where the unit ends up if it keeps blending the same path velocity with its inertia until that inertia
	 * has decayed: 1 + inertiaStrength turns, which is how far it takes to stop.
	 */
	static CFixedVector2D PredictPosition(const CFixedVector2D& pos, const CFixedVector2D& pathVelocity,
		const CFixedVector2D& inertiaVelocity, const fixed inertiaStrength, const fixed maxSpeed, const fixed dt);

	/**
	 * Reduce the sideways part of a velocity (relative to the unit's facing) by driftCorrection, and pay for it
	 * with forward speed: the lateral momentum being fought, but never more than the lateral movement removed.
	 * The result is clamped to maxSpeed. Does nothing if the velocity has no sideways part.
	 * @return true if the correction brought the unit to a full stop (deliberate, not an obstruction).
	 */
	static bool CorrectDrift(CFixedVector2D& velocity, const CFixedVector2D& inertiaVelocity,
		const CFixedVector2D& forwardDir, const fixed driftCorrection, const fixed maxSpeed);

	/**
	 * A velocity below 0.01 m/s, snapped to exactly zero. Decay can't do it: fixed-point rounding leaves a
	 * negative component stuck at -1 raw unit, and a unit creeping on it forever.
	 */
	static CFixedVector2D SnapNegligible(const CFixedVector2D& velocity)
	{
		return SnapBelow(velocity, NEGLIGIBLE_SPEED);
	}

	/**
	 * The velocity, or exactly zero if it's slower than speed.
	 */
	static CFixedVector2D SnapBelow(const CFixedVector2D& velocity, const fixed speed)
	{
		if (velocity.CompareLength(speed) < 0)
			return CFixedVector2D{};
		return velocity;
	}

	/**
	 * Below this speed a coasting unit (one without a move request) stops outright: the glide it has left,
	 * speed·dt·decay / (1 - decay), is under COASTING_REST_DISTANCE. Without it, a coasting unit spent its last several
	 * turns awake creeping a few centimetres at a time, the longest for units that keep the most inertia.
	 * @param decayPerTurn - share of the inertia kept over a turn of length dt.
	 */
	static fixed CoastingRestSpeed(const fixed decayPerTurn, const fixed dt)
	{
		// Keeping (almost) nothing, there's no glide to speak of: stop at any speed.
		if (decayPerTurn <= fixed::FromFraction(1, 100) || dt <= fixed::Zero())
			return fixed::FromInt(1000);
		const fixed speed{COASTING_REST_DISTANCE.Multiply(fixed::FromInt(1) - decayPerTurn) / decayPerTurn.Multiply(dt)};
		return std::max(speed, NEGLIGIBLE_SPEED);
	}

	/**
	 * Next turn's external inertia: what carried over from past pushes plus what was applied this turn,
	 * snapped to zero once negligible so it fully decays.
	 */
	static CFixedVector2D CarryExternalInertia(const CFixedVector2D& carried, const CFixedVector2D& applied)
	{
		return SnapNegligible(carried + applied);
	}
};

#endif // INCLUDED_MOMENTUMMODEL
