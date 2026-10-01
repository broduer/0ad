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
#include "HeadingControl.h"

#include "maths/MathUtil.h"

#include <algorithm>

namespace
{
/**
 * The largest value whose square still fits an entity_angle_t (15 integer bits, so just under 32768).
 * Only used to keep a squared comparison in range; a turn rate this large is not a real one.
 */
constexpr entity_angle_t SQUARABLE_LIMIT = entity_angle_t::FromInt(181);
}

entity_angle_t HeadingControl::PathFacingAngle(const entity_angle_t waypointAngle, const CFixedVector2D& facingVelocity,
	const fixed movementWeight)
{
	if (movementWeight <= fixed::Zero() || facingVelocity.IsZero())
		return waypointAngle;

	const entity_angle_t facingAngle{atan2_approx(facingVelocity.X, facingVelocity.Y)};
	return waypointAngle + WrapAngle(facingAngle - waypointAngle).Multiply(movementWeight);
}

bool HeadingControl::TurnWithMomentum(entity_angle_t& angle, fixed& angularVelocity, entity_angle_t targetAngle,
	const fixed maxRate, const fixed accel, const fixed dt, const fixed externalVelocity)
{
	// Callers can pass slightly un-normalized targets (e.g. a blended heading past PI), which could otherwise
	// never compare equal to the normalized angle.
	targetAngle = NormalizeAngle(targetAngle);

	const fixed angleDiff{WrapAngle(targetAngle - angle)};
	const fixed absAngleDiff{angleDiff.Absolute()};

	// How fast we can be turning and still stop on the target, braking by a·dt per turn. The fastest speed from which
	// the remaining whole steps (v, v - a·dt, ..., a·dt) still stop within the angle left:
	// s·(sqrt(1/4 + 2θ/(s·dt)) - 1/2) with s = a·dt, which rearranges to sqrt(s²/4 + 2·a·θ) - s/2 (no division, so no
	// overflow for tiny accelerations). The continuous-time sqrt(2·a·θ) would keep a unit turning fast right up to the
	// target, then stop it dead in one turn.
	const fixed maxStep{accel.Multiply(dt)};
	const fixed radicand{maxStep.Multiply(maxStep) / 4 + accel.Multiply(absAngleDiff) * 2};

	// Far from the target, the speed is capped at maxRate anyway, so skip the square root there. The cap wins when
	// sqrt(radicand) - s/2 >= maxRate, i.e. when radicand >= (maxRate + s/2)², which gives the same answer as the
	// rounded-down root would.
	const fixed capThreshold{maxRate + maxStep / 2};
	const bool capWins{capThreshold < SQUARABLE_LIMIT && radicand >= capThreshold.Multiply(capThreshold)};

	// Turn as fast as we can while still able to stop on the target.
	const fixed desiredVelocity{capWins ? maxRate :
		std::min(maxRate, std::max(fixed::Zero(), radicand.Sqrt() - maxStep / 2))};

	// Speed up or slow down towards it, turning the short way round, by at most maxStep per turn. The unit rotates
	// at its own turning plus the external spin, so it is the sum that aims for the target: with a spin, the unit
	// turns against it. Only its own part is capped at maxRate, which can drop between calls (e.g. while
	// staggered): an own turning faster than that is cut at once, a spin is not.
	const fixed targetVelocity{angleDiff > fixed::Zero() ? desiredVelocity : -desiredVelocity};
	angularVelocity = Clamp(angularVelocity + Clamp(targetVelocity - angularVelocity - externalVelocity, -maxStep, maxStep),
		-maxRate, maxRate);

	// Land on the target and stop if this step would reach or pass it while turning towards it. Stopping means
	// holding the spin off, which lasts until the spin has decayed.
	const fixed rotation{(angularVelocity + externalVelocity).Multiply(dt)};
	if (absAngleDiff <= rotation.Absolute() && (rotation > fixed::Zero()) == (angleDiff > fixed::Zero()))
	{
		angle = targetAngle;
		angularVelocity = Clamp(-externalVelocity, -maxRate, maxRate);
	}
	else
		angle = angle + rotation;

	angle = NormalizeAngle(angle);

	return angle == targetAngle && (angularVelocity + externalVelocity).IsZero();
}
