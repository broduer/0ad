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
#include "ImpactEffects.h"

#include "simulation2/helpers/ChargeConfig.h"
#include "simulation2/helpers/HeadingControl.h"
#include "simulation2/helpers/PushConfig.h"

namespace
{

fixed ClampSymmetric(const fixed value, const fixed limit)
{
	if (value.Absolute() > limit)
		return value > fixed::Zero() ? limit : -limit;
	return value;
}
} // anonymous namespace

void ImpactEffects::AddSpinImpulse(MotionState& state, const fixed impulse)
{
	state.impactAngularVelocity = ClampSymmetric(state.impactAngularVelocity + impulse, ChargeConfig::Get().spinMaxVelocity);
}

void ImpactEffects::AddRollImpulse(MotionState& state, const fixed impulse)
{
	if (state.maxRollAngle <= fixed::Zero())
	{
		state.rollVelocity = fixed::Zero();
		return;
	}

	state.rollVelocity = ClampSymmetric(state.rollVelocity + impulse, state.maxRollAngle.Multiply(ChargeConfig::Get().rollMaxVelocityScale));
	state.rollAngle = ClampSymmetric(state.rollAngle, state.maxRollAngle);
}

fixed ImpactEffects::RollFromVelocityChange(const MotionState& state, const CFixedVector2D& velocityChange)
{
	if (state.maxRollAngle <= fixed::Zero() || velocityChange.IsZero())
		return fixed::Zero();

	const ChargeConfig::Values& config{ChargeConfig::Get()};
	const CFixedVector2D right{HeadingControl::FacingDirection(state.angle).Perpendicular()};
	const fixed roll{ClampSymmetric(velocityChange.Dot(right).Multiply(config.rollStrength),
		state.maxRollAngle.Multiply(config.rollMaxVelocityScale))};
	return roll.Absolute() < config.rollImpulseMin ? fixed::Zero() : roll;
}

void ImpactEffects::DecaySpin(MotionState& state, const fixed dt)
{
	if (state.impactAngularVelocity.IsZero())
		return;

	// The decay is per IMPACT_REFERENCE_TIME, so the spin lasts as long at any turn length.
	state.impactAngularVelocity = state.impactAngularVelocity.Multiply(PushConfig::DecayFactor(fixed::FromInt(1) - state.spinDecay, dt));
	if (state.impactAngularVelocity.Absolute() < ChargeConfig::Get().spinRestVelocity)
		state.impactAngularVelocity = fixed::Zero();
}

void ImpactEffects::IntegrateRoll(MotionState& state, const fixed dt)
{
	if (state.maxRollAngle <= fixed::Zero())
		return;

	const ChargeConfig::Values& config{ChargeConfig::Get()};
	// Damped spring: acceleration = -k·angle - c·velocity.
	const fixed acceleration{-state.rollAngle.Multiply(config.rollSpring) + -state.rollVelocity.Multiply(config.rollDamping)};
	state.rollVelocity = state.rollVelocity + acceleration.Multiply(dt);
	state.rollAngle = ClampSymmetric(state.rollAngle + state.rollVelocity.Multiply(dt),
		state.maxRollAngle.Multiply(config.rollOvershootScale));

	if (state.rollVelocity.Absolute() < config.rollRestVelocity)
		state.rollVelocity = fixed::Zero();
}

void ImpactEffects::SettleRoll(MotionState& state)
{
	if (state.rollAngle.Absolute() <= ChargeConfig::Get().rollSettledAngle && state.rollVelocity.IsZero())
		state.rollAngle = fixed::Zero();
}
