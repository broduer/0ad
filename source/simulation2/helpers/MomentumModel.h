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
#include <span>
#include <vector>

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
	 * A solid body near the unit: the contact's unit normal, pointing away from that body, the gap left to it,
	 * whether the unit gives way to that body (see UnitCollision::ComputePushes), and whether that body is on
	 * an order.
	 */
	struct Contact
	{
		CFixedVector2D normal;
		entity_pos_t gap;
		// How fast that body is moving away from the unit along the normal, in m/s (zero if it isn't).
		fixed receding{};
		bool yield{false};
		bool moving{false};
	};

	/**
	 * Whether one of @p contacts holds @p velocity back: the unit steers into it faster than SlideAlongContacts
	 * lets it close the gap, so it is up against that body, not merely near one. With @p only, only the
	 * bodies it is true of count: those the unit gives way to, or those on an order.
	 */
	static bool HeldByContact(const CFixedVector2D& velocity, const std::span<const Contact> contacts, const fixed dt,
		const bool Contact::* const only = nullptr)
	{
		if (dt <= fixed::Zero())
			return false;
		for (const Contact& contact : contacts)
			if ((!only || contact.*only) && -velocity.Dot(contact.normal) > (contact.gap / 2) / dt + contact.receding)
				return true;
		return false;
	}

	/**
	 * A unit's own velocity against the solid bodies near it: what runs into a contact (against its normal) is
	 * cut to the speed that closes half its gap this turn, and what runs along it is kept, so the unit eases up
	 * to a body and slides along it rather than thrusting into it. The allowance falls with the gap and nowhere
	 * else: held off by a push, the unit settles where the two balance. A fixed speed cap, or a cut at the edge
	 * of a contact's reach, is a line the push shoves it back over one turn and it steps across the next.
	 * It is the closing of the gap that is limited, so the speed the other body moves away at is allowed on top:
	 * units walking the same way don't hold each other back, however close, and a column follows its head
	 * at the head's pace.
	 */
	static CFixedVector2D SlideAlongContacts(CFixedVector2D velocity, const std::span<const Contact> contacts, const fixed dt)
	{
		if (dt <= fixed::Zero())
			return velocity;
		for (const Contact& contact : contacts)
		{
			const fixed into{velocity.Dot(contact.normal)};
			const fixed allowed{-((contact.gap / 2) / dt + contact.receding)};
			if (into < allowed)
				velocity -= contact.normal.Multiply(into - allowed);
		}
		return velocity;
	}

	/**
	 * A velocity below 0.01 m/s, snapped to exactly zero.
	 * Decay alone never gets there: fixed-point Multiply rounds towards -infinity, so a small negative component stays
	 * negative. A coasting unit then moves by -1 raw unit a turn, its inertia is rebuilt from that movement (-1 / dt), and
	 * it creeps invisibly forever, never going back to sleep.
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
	 * Split a velocity change (a contact impulse or a knockback) between the unit's own momentum and its external
	 * momentum. The part of the own momentum that runs against the change is cancelled first, caped to the change's
	 * size: a unit stopped by what it runs into loses that speed.
	 */
	static CFixedVector2D AbsorbVelocityChange(CFixedVector2D& ownVelocity, const CFixedVector2D& velocityChange)
	{
		if (velocityChange.IsZero() || ownVelocity.IsZero())
			return velocityChange;
		const fixed size{velocityChange.Length()};
		if (size <= fixed::Zero())
			return velocityChange;
		const CFixedVector2D direction{velocityChange.Multiply(fixed::FromInt(1) / size)};
		const fixed against{-ownVelocity.Dot(direction)};
		if (against <= fixed::Zero())
			return velocityChange;
		const fixed cancelled{std::min(against, size)};
		ownVelocity += direction.Multiply(cancelled);
		return velocityChange - direction.Multiply(cancelled);
	}

	/**
	 * This turn's soft push, cut so that it never takes the push momentum along its own direction past the push's
	 * own speed, or past what the unit already carried that way if that is more.
	 */
	static CFixedVector2D ClampCumulativePush(const CFixedVector2D& carried, const CFixedVector2D& push)
	{
		if (push.IsZero())
			return push;
		const fixed size{push.Length()};
		if (size <= fixed::Zero())
			return push;
		const CFixedVector2D direction{push.Multiply(fixed::FromInt(1) / size)};
		const fixed along{carried.Dot(direction)};
		const fixed excess{along + size - std::max(size, along)};
		if (excess <= fixed::Zero())
			return push;
		return push - direction.Multiply(excess);
	}

	/**
	 * Next turn's external inertia: what carried over from past pushes plus what was applied this turn,
	 * snapped to zero once negligible so it fully decays.
	 */
	static CFixedVector2D CarryExternalInertia(const CFixedVector2D& carried, const CFixedVector2D& applied)
	{
		return SnapNegligible(carried + applied);
	}

	// Spins slower than this (radians/second) are dropped for the same reason as NEGLIGIBLE_SPEED: a
	// decaying spin never reaches zero on its own in fixed point, and would turn the unit by a raw unit a
	// turn forever.
	static constexpr fixed NEGLIGIBLE_SPIN{fixed::FromFraction(1, 100)};

	/**
	 * Next turn's carried push spin: what was carried into this turn plus what this turn's pushes added,
	 * or exactly zero once it is negligible. The scalar counterpart of CarryExternalInertia.
	 */
	static fixed CarryPushSpin(const fixed carried, const fixed applied)
	{
		const fixed sum{carried + applied};
		return sum.Absolute() < NEGLIGIBLE_SPIN ? fixed::Zero() : sum;
	}
};

#endif // INCLUDED_MOMENTUMMODEL
