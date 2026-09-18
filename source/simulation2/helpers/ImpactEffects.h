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

#ifndef INCLUDED_IMPACTEFFECTS
#define INCLUDED_IMPACTEFFECTS

#include "maths/Fixed.h"
#include "simulation2/helpers/MotionState.h"

/**
 * What an impact (e.g. a charge) does to a unit's orientation, on top of its own movement:
 * - spin: extra turning around the vertical axis (MotionState::impactAngularVelocity), which decays by friction;
 * - roll: tilting sideways (MotionState::rollAngle/rollVelocity), a damped spring back to level,
 *   only for units with a max roll angle (e.g. ships).
 * Both settle to exactly zero, so an impacted unit goes back to sleep on its own.
 * Stateless: only reads and writes those fields of the given MotionState, never components.
 */
class ImpactEffects
{
public:
	/**
	 * Add a spin impulse (radians/second), capped at a fast but not absurd spin.
	 */
	static void AddSpinImpulse(MotionState& state, const fixed impulse);

	/**
	 * Add a roll impulse (radians/second), capped relative to the unit's max roll angle.
	 * A unit that can't roll ignores it.
	 */
	static void AddRollImpulse(MotionState& state, const fixed impulse);

	/**
	 * Whether the unit still has a roll to show (it can roll, and isn't level and at rest).
	 */
	static bool HasRoll(const MotionState& state)
	{
		return state.maxRollAngle > fixed::Zero() && (!state.rollAngle.IsZero() || !state.rollVelocity.IsZero());
	}

	/**
	 * Whether spin or roll still needs integrating, so the unit must be kept awake.
	 * (Roll only decays for units that can roll: a stray roll value on one that can't mustn't keep it awake forever.
	 * Spin decays regardless.)
	 */
	static bool IsActive(const MotionState& state)
	{
		return HasRoll(state) || !state.impactAngularVelocity.IsZero();
	}

	/**
	 * One turn of spin: rotate state.angle and decay the spin.
	 */
	static void IntegrateSpin(MotionState& state, const fixed dt);

	/**
	 * One turn of roll: the spring pulls the roll back towards level, damped, within 1.5 times the max roll angle.
	 */
	static void IntegrateRoll(MotionState& state, const fixed dt);

	/**
	 * Once the roll is at rest and below the visible threshold, set it to exactly level,
	 * so the last tilt applied to the unit doesn't stick.
	 */
	static void SettleRoll(MotionState& state);
};

#endif // INCLUDED_IMPACTEFFECTS
