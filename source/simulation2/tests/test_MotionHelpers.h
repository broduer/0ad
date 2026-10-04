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

#include "lib/self_test.h"

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/components/ICmpObstructionManager.h"
#include "simulation2/helpers/ChargeConfig.h"
#include "simulation2/helpers/Geometry.h"
#include "simulation2/helpers/HeadingControl.h"
#include "simulation2/helpers/ImpactEffects.h"
#include "simulation2/helpers/MotionState.h"
#include "simulation2/helpers/MomentumModel.h"
#include "simulation2/helpers/PathFollowing.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/helpers/UnitCollision.h"

#include <cmath>
#include <vector>
#include "simulation2/helpers/Position.h"

class TestMotionHelpers : public CxxTest::TestSuite
{
public:
	void test_WrapAngle()
	{
		const entity_angle_t pi{entity_angle_t::Pi()};
		const entity_angle_t quarter{fixed::FromFraction(1, 4)};

		TS_ASSERT_EQUALS(HeadingControl::WrapAngle(fixed::Zero()), fixed::Zero());
		TS_ASSERT_EQUALS(HeadingControl::WrapAngle(pi), pi);
		TS_ASSERT_EQUALS(HeadingControl::WrapAngle(-pi), -pi);
		TS_ASSERT_EQUALS(HeadingControl::WrapAngle(pi + quarter), quarter - pi);
		TS_ASSERT_EQUALS(HeadingControl::WrapAngle(-pi - quarter), pi - quarter);

		// A single correction: an angle beyond 3·PI stays out of range (NormalizeAngle is for those).
		const entity_angle_t farOut{pi * 3 + quarter};
		TS_ASSERT_EQUALS(HeadingControl::WrapAngle(farOut), farOut - pi * 2);
		TS_ASSERT(HeadingControl::WrapAngle(farOut) > pi);
	}

	void test_NormalizeAngle()
	{
		const entity_angle_t pi{entity_angle_t::Pi()};
		const entity_angle_t quarter{fixed::FromFraction(1, 4)};

		TS_ASSERT_EQUALS(HeadingControl::NormalizeAngle(quarter), quarter);
		TS_ASSERT_EQUALS(HeadingControl::NormalizeAngle(pi * 3 + quarter), quarter - pi);
		TS_ASSERT_EQUALS(HeadingControl::NormalizeAngle(-pi * 5 - quarter), pi - quarter);
	}

	void test_PathFacingAngle()
	{
		const CFixedVector2D north{fixed::Zero(), fixed::FromInt(10)};
		const CFixedVector2D east{fixed::FromInt(10), fixed::Zero()};
		const entity_angle_t northAngle{atan2_approx(north.X, north.Y)};
		const entity_angle_t eastAngle{atan2_approx(east.X, east.Y)};

		// No weight, or nowhere to go: face the waypoint.
		TS_ASSERT_EQUALS(HeadingControl::PathFacingAngle(northAngle, east, fixed::Zero()), northAngle);
		TS_ASSERT_EQUALS(HeadingControl::PathFacingAngle(northAngle, CFixedVector2D{}, fixed::FromInt(1)), northAngle);

		// Full weight faces the direction of travel, half weight faces halfway.
		TS_ASSERT_EQUALS(HeadingControl::PathFacingAngle(northAngle, east, fixed::FromInt(1)), eastAngle);
		TS_ASSERT_EQUALS(HeadingControl::PathFacingAngle(northAngle, east, fixed::FromFraction(1, 2)),
			northAngle + (eastAngle - northAngle).Multiply(fixed::FromFraction(1, 2)));
	}

	void test_TurnWithMomentum_reaches_target_without_overshoot()
	{
		const entity_angle_t target{fixed::FromInt(2)};
		const fixed maxRate{fixed::FromInt(3)};
		const fixed accel{fixed::FromInt(6)};
		const fixed dt{fixed::FromFraction(1, 5)};

		entity_angle_t angle{fixed::Zero()};
		fixed angularVelocity{fixed::Zero()};
		bool aligned{false};
		for (int turn{0}; turn < 100 && !aligned; ++turn)
		{
			aligned = HeadingControl::TurnWithMomentum(angle, angularVelocity, target, maxRate, accel, dt);
			TS_ASSERT(angle <= target);
			TS_ASSERT(angularVelocity >= fixed::Zero());
			TS_ASSERT(angularVelocity <= maxRate);
		}
		TS_ASSERT(aligned);
		TS_ASSERT_EQUALS(angle, target);
		TS_ASSERT_EQUALS(angularVelocity, fixed::Zero());
	}

	void test_TurnWithMomentum_takes_the_short_way_round()
	{
		const entity_angle_t pi{entity_angle_t::Pi()};
		// From below PI to above -PI: a 1 rad turn through PI, not almost a full circle the other way.
		// Far enough apart that the first step doesn't already snap onto the target.
		const entity_angle_t start{pi - fixed::FromFraction(1, 2)};
		entity_angle_t angle{start};
		fixed angularVelocity{fixed::Zero()};
		const entity_angle_t target{-pi + fixed::FromFraction(1, 2)};

		TS_ASSERT(!HeadingControl::TurnWithMomentum(angle, angularVelocity, target, fixed::FromInt(3), fixed::FromInt(6), fixed::FromFraction(1, 5)));
		TS_ASSERT(angularVelocity > fixed::Zero());
		TS_ASSERT(angle > start);
		TS_ASSERT(angle <= pi);
	}

	void test_TurnWithMomentum_holds_the_heading_against_a_spin()
	{
		const fixed maxRate{fixed::FromInt(4)};
		const fixed accel{fixed::FromInt(4)};
		const fixed dt{fixed::FromFraction(1, 5)};
		const entity_angle_t target{fixed::FromInt(1)};

		// Facing the target and spun at 1 rad/s: the unit turns against the spin by accel·dt, and is rotated by
		// what it couldn't cancel, not by the whole spin.
		entity_angle_t angle{target};
		fixed angularVelocity{fixed::Zero()};
		const fixed spin{fixed::FromInt(1)};
		TS_ASSERT(!HeadingControl::TurnWithMomentum(angle, angularVelocity, target, maxRate, accel, dt, spin));
		TS_ASSERT_EQUALS(angularVelocity, -accel.Multiply(dt));
		TS_ASSERT_EQUALS(angle, target + (spin - accel.Multiply(dt)).Multiply(dt));

		// Kept up, the spin is fully held off within a few turns: the unit is back on its heading and reports
		// aligned only once it no longer rotates.
		bool aligned{false};
		for (int turn{0}; turn < 20 && !aligned; ++turn)
		{
			aligned = HeadingControl::TurnWithMomentum(angle, angularVelocity, target, maxRate, accel, dt, spin);
			TS_ASSERT(angularVelocity >= -maxRate);
		}
		TS_ASSERT(aligned);
		TS_ASSERT_EQUALS(angle, target);
		TS_ASSERT_EQUALS(angularVelocity, -spin);

		// A spin faster than the unit can turn keeps rotating it, while the unit builds up its own turning against
		// it by accel·dt a turn. (Four turns: after that the target is nearer the other way round, and the unit
		// rides the spin towards it instead.)
		angle = target;
		angularVelocity = fixed::Zero();
		const fixed fastSpin{fixed::FromInt(6)};
		for (int turn{0}; turn < 4; ++turn)
			HeadingControl::TurnWithMomentum(angle, angularVelocity, target, maxRate, accel, dt, fastSpin);
		TS_ASSERT_EQUALS(angularVelocity, -accel.Multiply(dt) * 4);
		TS_ASSERT(angle != target);

		// Without a spin, nothing changes: a unit on its heading stays put.
		angle = target;
		angularVelocity = fixed::Zero();
		TS_ASSERT(HeadingControl::TurnWithMomentum(angle, angularVelocity, target, maxRate, accel, dt));
		TS_ASSERT_EQUALS(angle, target);
	}

	void test_TurnWithMomentum_accepts_unnormalized_target()
	{
		const entity_angle_t pi{entity_angle_t::Pi()};
		entity_angle_t angle{fixed::FromFraction(1, 2)};
		fixed angularVelocity{fixed::Zero()};

		// Already facing the target, given 2·PI too high: aligned at once.
		TS_ASSERT(HeadingControl::TurnWithMomentum(angle, angularVelocity, angle + pi * 2,
			fixed::FromInt(3), fixed::FromInt(6), fixed::FromFraction(1, 5)));
		TS_ASSERT_EQUALS(angle, fixed::FromFraction(1, 2));
	}

	void test_ClampSpeed()
	{
		const CFixedVector2D velocity{fixed::FromInt(3), fixed::FromInt(4)};

		TS_ASSERT_EQUALS(MomentumModel::ClampSpeed(velocity, fixed::FromInt(10)), velocity);
		TS_ASSERT_EQUALS(MomentumModel::ClampSpeed(velocity, fixed::FromInt(5)), velocity);
		TS_ASSERT_EQUALS(MomentumModel::ClampSpeed(CFixedVector2D{}, fixed::Zero()), CFixedVector2D{});

		const CFixedVector2D clamped{MomentumModel::ClampSpeed(velocity, fixed::FromInt(1))};
		TS_ASSERT_DELTA(clamped.Length().ToDouble(), 1.0, 0.001);
		// Same direction.
		TS_ASSERT_DELTA(clamped.X.ToDouble(), 0.6, 0.001);
		TS_ASSERT_DELTA(clamped.Y.ToDouble(), 0.8, 0.001);
	}

	void test_BlendWithInertia()
	{
		const CFixedVector2D path{fixed::FromInt(2), fixed::Zero()};
		const CFixedVector2D inertia{fixed::Zero(), fixed::FromInt(2)};

		// No inertia: the path velocity as is.
		TS_ASSERT_EQUALS(MomentumModel::BlendWithInertia(path, inertia, fixed::Zero()), path);
		// Strength 1: the average.
		TS_ASSERT_EQUALS(MomentumModel::BlendWithInertia(path, inertia, fixed::FromInt(1)),
			CFixedVector2D(fixed::FromInt(1), fixed::FromInt(1)));
	}

	void test_PredictPosition()
	{
		const CFixedVector2D pos{fixed::FromInt(100), fixed::FromInt(100)};
		const fixed dt{fixed::FromInt(1)};

		// No inertia: a single turn at the path velocity, clamped to max speed.
		TS_ASSERT_EQUALS(MomentumModel::PredictPosition(pos, CFixedVector2D(fixed::Zero(), fixed::FromInt(10)),
				CFixedVector2D{}, fixed::Zero(), fixed::FromInt(5), dt),
			CFixedVector2D(fixed::FromInt(100), fixed::FromInt(105)));

		// Strength 2, coasting with no path velocity: three turns of decaying inertia, 2 + 4/3 + 8/9 = 4.22 in total.
		const CFixedVector2D coasted{MomentumModel::PredictPosition(pos, CFixedVector2D{},
			CFixedVector2D(fixed::Zero(), fixed::FromInt(3)), fixed::FromInt(2), fixed::FromInt(10), dt)};
		TS_ASSERT_EQUALS(coasted.X, fixed::FromInt(100));
		TS_ASSERT_DELTA(coasted.Y.ToDouble(), 104.222, 0.01);
	}

	void test_CorrectDrift_leaves_straight_movement_alone()
	{
		CFixedVector2D velocity{fixed::Zero(), fixed::FromInt(5)};
		TS_ASSERT(!MomentumModel::CorrectDrift(velocity, velocity, HeadingControl::FacingDirection(fixed::Zero()), fixed::FromInt(1), fixed::FromInt(10)));
		TS_ASSERT_EQUALS(velocity, CFixedVector2D(fixed::Zero(), fixed::FromInt(5)));
	}

	void test_CorrectDrift_full_correction()
	{
		// Facing +Y, moving 3 sideways and 4 forwards, fighting 3 of lateral inertia:
		// the sideways part is removed and costs 3 of the forward speed.
		CFixedVector2D velocity{fixed::FromInt(3), fixed::FromInt(4)};
		TS_ASSERT(!MomentumModel::CorrectDrift(velocity, velocity, HeadingControl::FacingDirection(fixed::Zero()), fixed::FromInt(1), fixed::FromInt(10)));
		TS_ASSERT_DELTA(velocity.X.ToDouble(), 0.0, 0.001);
		TS_ASSERT_DELTA(velocity.Y.ToDouble(), 1.0, 0.001);

		// More sideways momentum than forward speed: the unit stops to turn.
		CFixedVector2D sideways{fixed::FromInt(4), fixed::FromInt(1)};
		TS_ASSERT(MomentumModel::CorrectDrift(sideways, CFixedVector2D(fixed::FromInt(4), fixed::Zero()),
			HeadingControl::FacingDirection(fixed::Zero()), fixed::FromInt(1), fixed::FromInt(10)));
		TS_ASSERT_EQUALS(sideways, CFixedVector2D{});
	}

	void test_CorrectDrift_never_speeds_up()
	{
		const fixed corrections[]{fixed::FromFraction(1, 4), fixed::FromFraction(1, 2), fixed::FromInt(1)};
		const entity_angle_t angles[]{fixed::Zero(), fixed::FromFraction(7, 10), -fixed::FromInt(2), fixed::FromInt(3)};
		const CFixedVector2D velocities[]{
			{fixed::FromInt(3), fixed::FromInt(4)},
			{-fixed::FromInt(5), fixed::FromInt(1)},
			{fixed::FromInt(2), -fixed::FromInt(6)}};

		for (const fixed correction : corrections)
			for (const entity_angle_t angle : angles)
				for (const CFixedVector2D& original : velocities)
				{
					CFixedVector2D velocity{original};
					MomentumModel::CorrectDrift(velocity, original, HeadingControl::FacingDirection(angle), correction, fixed::FromInt(100));
					TS_ASSERT_LESS_THAN_EQUALS(velocity.Length().ToDouble(), original.Length().ToDouble() + 0.001);
				}
	}

	void test_SnapNegligible()
	{
		// Why it's needed: decay rounds towards -infinity, so a tiny negative velocity never reaches zero on its own.
		const fixed creep{-fixed::Epsilon() * 5};
		TS_ASSERT_EQUALS(creep.Multiply(fixed::FromFraction(9, 10)), creep);
		// ...and moving at it for a turn still moves by a raw unit, from which the inertia is rebuilt.
		TS_ASSERT_EQUALS(creep.Multiply(fixed::FromFraction(1, 5)), -fixed::Epsilon());

		TS_ASSERT_EQUALS(MomentumModel::SnapNegligible(CFixedVector2D(creep, creep)), CFixedVector2D{});
		TS_ASSERT_EQUALS(MomentumModel::SnapNegligible(CFixedVector2D(fixed::FromFraction(1, 200), -fixed::FromFraction(1, 200))),
			CFixedVector2D{});

		// Anything from 0.01 up is kept.
		const CFixedVector2D slow{fixed::FromFraction(1, 100), fixed::Zero()};
		TS_ASSERT_EQUALS(MomentumModel::SnapNegligible(slow), slow);
		const CFixedVector2D moving{-fixed::FromInt(3), fixed::FromInt(4)};
		TS_ASSERT_EQUALS(MomentumModel::SnapNegligible(moving), moving);
	}

	void test_CoastingRestSpeed()
	{
		const fixed dt{fixed::FromFraction(1, 5)};

		// Cavalry keep half their inertia a turn: from 0.5 m/s the glide left is 0.5·0.2·(0.5 / 0.5) = 0.1 m.
		TS_ASSERT_DELTA(MomentumModel::CoastingRestSpeed(fixed::FromFraction(1, 2), dt).ToDouble(), 0.5, 0.001);
		// The more inertia a unit keeps, the slower it must be before stopping outright.
		TS_ASSERT_DELTA(MomentumModel::CoastingRestSpeed(fixed::FromFraction(1, 3), dt).ToDouble(), 1.0, 0.001);
		TS_ASSERT(MomentumModel::CoastingRestSpeed(fixed::FromFraction(9, 10), dt) < fixed::FromFraction(6, 100));
		// Never below negligible, even for a unit that keeps (almost) everything.
		TS_ASSERT_EQUALS(MomentumModel::CoastingRestSpeed(fixed::FromInt(1), dt), MomentumModel::NEGLIGIBLE_SPEED);
		// Keeping nothing, any speed stops.
		TS_ASSERT(MomentumModel::CoastingRestSpeed(fixed::Zero(), dt) > fixed::FromInt(100));

		const CFixedVector2D slow{fixed::FromFraction(3, 10), fixed::FromFraction(3, 10)};
		TS_ASSERT_EQUALS(MomentumModel::SnapBelow(slow, fixed::FromFraction(1, 2)), CFixedVector2D{});
		TS_ASSERT_EQUALS(MomentumModel::SnapBelow(slow, fixed::FromFraction(4, 10)), slow);
	}

	void test_AbsorbVelocityChange()
	{
		const CFixedVector2D east{fixed::FromInt(6), fixed::Zero()};

		// A unit walking east at 6 m/s stopped by a 4 m/s change westwards: 4 m/s of its own momentum is gone,
		// and nothing is left to push it back.
		CFixedVector2D own{east};
		CFixedVector2D left{MomentumModel::AbsorbVelocityChange(own, CFixedVector2D{fixed::FromInt(-4), fixed::Zero()})};
		TS_ASSERT_DELTA(own.X.ToDouble(), 2.0, 0.001);
		TS_ASSERT(left.IsZero());

		// A change larger than its speed: all its own momentum goes, and the rest pushes it back. Within a
		// rounding error of the unit direction.
		own = east;
		left = MomentumModel::AbsorbVelocityChange(own, CFixedVector2D{fixed::FromInt(-10), fixed::Zero()});
		TS_ASSERT_LESS_THAN(own.Length().ToDouble(), 0.01);
		TS_ASSERT_DELTA(left.X.ToDouble(), -4.0, 0.01);

		// Only the part against the change is taken: a change northwards leaves an eastward walk alone.
		own = east;
		left = MomentumModel::AbsorbVelocityChange(own, CFixedVector2D{fixed::Zero(), fixed::FromInt(3)});
		TS_ASSERT_EQUALS(own, east);
		TS_ASSERT_DELTA(left.Y.ToDouble(), 3.0, 0.001);

		// A unit standing still, or going the change's way, keeps it all as external momentum.
		own = CFixedVector2D{};
		left = MomentumModel::AbsorbVelocityChange(own, CFixedVector2D{fixed::FromInt(-4), fixed::Zero()});
		TS_ASSERT_DELTA(left.X.ToDouble(), -4.0, 0.001);
		own = east;
		left = MomentumModel::AbsorbVelocityChange(own, CFixedVector2D{fixed::FromInt(4), fixed::Zero()});
		TS_ASSERT_EQUALS(own, east);
		TS_ASSERT_DELTA(left.X.ToDouble(), 4.0, 0.001);
	}

	void test_ClampCumulativePush()
	{
		const CFixedVector2D push{fixed::FromFraction(14, 5), fixed::Zero()};  // 2.8 m/s east
		const auto along = [&](const CFixedVector2D& carried) {
			return (carried + MomentumModel::ClampCumulativePush(carried, push)).X.ToDouble();
		};

		// A single shove from rest is whole.
		TS_ASSERT_DELTA(along(CFixedVector2D{}), 2.8, 0.01);
		// Repeated while the momentum it left is still there: topped up to the push's speed, not added on top.
		TS_ASSERT_DELTA(along(CFixedVector2D{fixed::FromFraction(5, 2), fixed::Zero()}), 2.8, 0.01);
		// Already faster that way, from a hit: nothing more.
		TS_ASSERT_DELTA(along(CFixedVector2D{fixed::FromInt(6), fixed::Zero()}), 6.0, 0.01);
		// Against the momentum: whole, as before.
		TS_ASSERT_DELTA(along(CFixedVector2D{fixed::FromInt(-5), fixed::Zero()}), -2.2, 0.01);

		// Momentum across the push is left alone.
		const CFixedVector2D across{fixed::Zero(), fixed::FromInt(4)};
		const CFixedVector2D result{across + MomentumModel::ClampCumulativePush(across, push)};
		TS_ASSERT_DELTA(result.X.ToDouble(), 2.8, 0.01);
		TS_ASSERT_DELTA(result.Y.ToDouble(), 4.0, 0.01);

		// Repeated for many turns with ships' Drifting, the momentum stays at the push's speed instead of building up.
		CFixedVector2D momentum;
		for (int turn = 0; turn < 30; ++turn)
		{
			momentum = momentum.Multiply(fixed::FromFraction(9, 10));
			momentum += MomentumModel::ClampCumulativePush(momentum, push);
		}
		TS_ASSERT_DELTA(momentum.X.ToDouble(), 2.8, 0.01);
	}

	void test_CarryExternalInertia()
	{
		const CFixedVector2D tiny{fixed::FromFraction(1, 1000), fixed::Zero()};
		TS_ASSERT_EQUALS(MomentumModel::CarryExternalInertia(tiny, tiny), CFixedVector2D{});

		const CFixedVector2D carried{fixed::FromInt(1), fixed::Zero()};
		const CFixedVector2D applied{fixed::Zero(), fixed::FromInt(1)};
		TS_ASSERT_EQUALS(MomentumModel::CarryExternalInertia(carried, applied), CFixedVector2D(fixed::FromInt(1), fixed::FromInt(1)));
	}

	void test_AddSpinImpulse_is_capped()
	{
		MotionState state{nullptr, nullptr, nullptr};
		ImpactEffects::AddSpinImpulse(state, fixed::FromInt(2));
		TS_ASSERT_EQUALS(state.impactAngularVelocity, fixed::FromInt(2));
		ImpactEffects::AddSpinImpulse(state, fixed::FromInt(5));
		TS_ASSERT_EQUALS(state.impactAngularVelocity, fixed::FromInt(6));
		ImpactEffects::AddSpinImpulse(state, -fixed::FromInt(20));
		TS_ASSERT_EQUALS(state.impactAngularVelocity, -fixed::FromInt(6));
	}

	void test_AddRollImpulse()
	{
		MotionState rigid{nullptr, nullptr, nullptr};
		rigid.rollVelocity = fixed::FromInt(1);
		ImpactEffects::AddRollImpulse(rigid, fixed::FromInt(1));
		TS_ASSERT_EQUALS(rigid.rollVelocity, fixed::Zero());

		// Max roll 0.5: velocity capped at 1 per second, angle at 0.5.
		MotionState ship{nullptr, nullptr, nullptr};
		ship.maxRollAngle = fixed::FromFraction(1, 2);
		ship.rollAngle = -fixed::FromInt(1);
		ImpactEffects::AddRollImpulse(ship, fixed::FromInt(3));
		TS_ASSERT_EQUALS(ship.rollVelocity, fixed::FromInt(1));
		TS_ASSERT_EQUALS(ship.rollAngle, -fixed::FromFraction(1, 2));
	}

	void test_DecaySpin()
	{
		const fixed dt{fixed::FromFraction(1, 5)};
		MotionState state{nullptr, nullptr, nullptr};
		// The default SpinDecay loses 0.15 of the spin every 0.2 s.
		const fixed keep{fixed::FromInt(1) - fixed::FromFraction(15, 100)};

		// Decays by friction, without touching the angle: rotating by the spin is the caller's job.
		state.angle = fixed::FromInt(1);
		state.impactAngularVelocity = fixed::FromInt(1);
		ImpactEffects::DecaySpin(state, dt);
		TS_ASSERT_EQUALS(state.angle, fixed::FromInt(1));
		TS_ASSERT_EQUALS(state.impactAngularVelocity, keep);

		// Down to exactly zero.
		for (int turn{0}; turn < 100 && ImpactEffects::IsActive(state); ++turn)
			ImpactEffects::DecaySpin(state, dt);
		TS_ASSERT(!ImpactEffects::IsActive(state));

		// An invisible spin stops right away instead of keeping the unit awake: 0.04 rad/s turns under a hundredth of a
		// radian, then snaps. A visible one keeps going.
		state.impactAngularVelocity = fixed::FromFraction(4, 100);
		ImpactEffects::DecaySpin(state, dt);
		TS_ASSERT_EQUALS(state.impactAngularVelocity, fixed::Zero());
		state.impactAngularVelocity = fixed::FromFraction(1, 10);
		ImpactEffects::DecaySpin(state, dt);
		TS_ASSERT_EQUALS(state.impactAngularVelocity, fixed::FromFraction(1, 10).Multiply(keep));

		// The decay is per 0.2 s, not per turn: a 0.4 s turn keeps the share of two 0.2 s turns.
		state.impactAngularVelocity = fixed::FromInt(1);
		ImpactEffects::DecaySpin(state, fixed::FromFraction(2, 5));
		TS_ASSERT_EQUALS(state.impactAngularVelocity, keep.Multiply(keep));

		// A unit's own SpinDecay replaces the default.
		state.spinDecay = fixed::FromFraction(1, 2);
		state.impactAngularVelocity = fixed::FromInt(1);
		ImpactEffects::DecaySpin(state, dt);
		TS_ASSERT_EQUALS(state.impactAngularVelocity, fixed::FromFraction(1, 2));
	}

	void test_IntegrateRoll_needs_a_max_roll_angle()
	{
		MotionState state{nullptr, nullptr, nullptr};
		state.rollAngle = fixed::FromInt(1);
		ImpactEffects::IntegrateRoll(state, fixed::FromFraction(1, 5));
		TS_ASSERT_EQUALS(state.rollAngle, fixed::FromInt(1));
		// A stray roll on a unit that can't roll doesn't keep it awake.
		TS_ASSERT(!ImpactEffects::HasRoll(state));
		TS_ASSERT(!ImpactEffects::IsActive(state));
	}

	void test_IntegrateRoll_settles_level()
	{
		const fixed dt{fixed::FromFraction(1, 5)};
		MotionState ship{nullptr, nullptr, nullptr};
		ship.maxRollAngle = fixed::FromFraction(1, 2);
		ImpactEffects::AddRollImpulse(ship, fixed::FromInt(1));

		// As the motion manager does it: integrate, then settle while there is a roll to show.
		int turn{0};
		for (; turn < 2000 && ImpactEffects::HasRoll(ship); ++turn)
		{
			ImpactEffects::IntegrateRoll(ship, dt);
			TS_ASSERT_LESS_THAN_EQUALS(ship.rollAngle.Absolute(), ship.maxRollAngle.Multiply(fixed::FromFraction(3, 2)));
			if (ImpactEffects::HasRoll(ship))
				ImpactEffects::SettleRoll(ship);
		}
		TS_ASSERT(!ImpactEffects::HasRoll(ship));
		TS_ASSERT_EQUALS(ship.rollAngle, fixed::Zero());
		TS_ASSERT_EQUALS(ship.rollVelocity, fixed::Zero());
	}

	static WaypointPath MakePath(const std::vector<int>& xs)
	{
		// Waypoints are stored in reverse: the current one is at the back.
		WaypointPath path;
		for (const int x : xs)
			path.m_Waypoints.push_back(Waypoint{fixed::FromInt(x), fixed::Zero()});
		return path;
	}

	void test_CurrentWaypoint()
	{
		const WaypointPath none;
		TS_ASSERT(!PathFollowing::CurrentWaypoint(none, none));
		TS_ASSERT_EQUALS(*PathFollowing::CurrentWaypoint(none, MakePath({2, 1})), CFixedVector2D(fixed::FromInt(1), fixed::Zero()));
		TS_ASSERT_EQUALS(*PathFollowing::CurrentWaypoint(MakePath({4, 3}), MakePath({2, 1})), CFixedVector2D(fixed::FromInt(3), fixed::Zero()));
	}

	void test_NextWaypoint()
	{
		const WaypointPath none;
		TS_ASSERT(!PathFollowing::NextWaypoint(none, none));
		// Within the short path.
		TS_ASSERT_EQUALS(*PathFollowing::NextWaypoint(MakePath({4, 3}), MakePath({2, 1})), CFixedVector2D(fixed::FromInt(4), fixed::Zero()));
		// From the end of the short path onto the long path.
		TS_ASSERT_EQUALS(*PathFollowing::NextWaypoint(MakePath({3}), MakePath({2, 1})), CFixedVector2D(fixed::FromInt(1), fixed::Zero()));
		// Within the long path.
		TS_ASSERT_EQUALS(*PathFollowing::NextWaypoint(none, MakePath({2, 1})), CFixedVector2D(fixed::FromInt(2), fixed::Zero()));
		// The last waypoint has no next one.
		TS_ASSERT(!PathFollowing::NextWaypoint(none, MakePath({1})));
		TS_ASSERT(!PathFollowing::NextWaypoint(MakePath({3}), none));
	}

	void test_SteeringVelocity_straight_ahead()
	{
		const fixed maxSpeed{fixed::FromInt(10)};
		TS_ASSERT_EQUALS(PathFollowing::SteeringVelocity(CFixedVector2D{}, HeadingControl::FacingDirection(fixed::Zero()), fixed::Zero(), maxSpeed,
			fixed::Zero(), fixed::FromInt(1), false), CFixedVector2D{});

		// Facing +Y, waypoint ahead: full speed towards it.
		const CFixedVector2D ahead{PathFollowing::SteeringVelocity(CFixedVector2D(fixed::Zero(), fixed::FromInt(50)),
			HeadingControl::FacingDirection(fixed::Zero()), fixed::Zero(), maxSpeed, fixed::Zero(), fixed::FromInt(1), false)};
		TS_ASSERT_DELTA(ahead.X.ToDouble(), 0.0, 0.001);
		TS_ASSERT_DELTA(ahead.Y.ToDouble(), 10.0, 0.001);
	}

	void test_SteeringVelocity_never_reverses()
	{
		const fixed maxSpeed{fixed::FromInt(10)};
		const CFixedVector2D behind{fixed::Zero(), -fixed::FromInt(50)};

		// No throttle kept while turning: stops pushing.
		const CFixedVector2D idle{PathFollowing::SteeringVelocity(behind, HeadingControl::FacingDirection(fixed::Zero()), maxSpeed, maxSpeed,
			fixed::Zero(), fixed::FromInt(1), false)};
		TS_ASSERT_DELTA(idle.Length().ToDouble(), 0.0, 0.001);

		// A turn braking of 0.25 keeps 75% of the throttle forwards at full speed...
		const CFixedVector2D carving{PathFollowing::SteeringVelocity(behind, HeadingControl::FacingDirection(fixed::Zero()), maxSpeed, maxSpeed,
			fixed::Zero(), fixed::FromFraction(1, 4), false)};
		TS_ASSERT_DELTA(carving.Y.ToDouble(), 7.5, 0.001);

		// ...but a unit at rest has no throttle to keep: it turns first...
		const CFixedVector2D resting{PathFollowing::SteeringVelocity(behind, HeadingControl::FacingDirection(fixed::Zero()), fixed::Zero(), maxSpeed,
			fixed::Zero(), fixed::FromFraction(1, 4), false)};
		TS_ASSERT_DELTA(resting.Length().ToDouble(), 0.0, 0.001);

		// ...unless its inertia says it can't cut its thrust dead: with inertia 9, 90% of the full-speed share.
		const CFixedVector2D heavy{PathFollowing::SteeringVelocity(behind, HeadingControl::FacingDirection(fixed::Zero()), fixed::Zero(), maxSpeed,
			fixed::FromInt(9), fixed::FromFraction(1, 4), false)};
		TS_ASSERT_DELTA(heavy.Y.ToDouble(), 6.75, 0.001);

		// ...and never while avoiding a collision.
		const CFixedVector2D avoiding{PathFollowing::SteeringVelocity(behind, HeadingControl::FacingDirection(fixed::Zero()), maxSpeed, maxSpeed,
			fixed::Zero(), fixed::FromFraction(1, 4), true)};
		TS_ASSERT_DELTA(avoiding.Length().ToDouble(), 0.0, 0.001);
	}

	void test_SteeringVelocity_limits_sideways_at_speed()
	{
		const fixed maxSpeed{fixed::FromInt(10)};
		const CFixedVector2D side{fixed::FromInt(50), fixed::Zero()};

		// Standing: all of the sideways movement.
		const CFixedVector2D standing{PathFollowing::SteeringVelocity(side, HeadingControl::FacingDirection(fixed::Zero()), fixed::Zero(), maxSpeed,
			fixed::FromInt(9), fixed::FromInt(1), false)};
		TS_ASSERT_DELTA(standing.X.Absolute().ToDouble(), 10.0, 0.001);

		// At full speed with inertia 9: a tenth of it.
		const CFixedVector2D moving{PathFollowing::SteeringVelocity(side, HeadingControl::FacingDirection(fixed::Zero()), maxSpeed, maxSpeed,
			fixed::FromInt(9), fixed::FromInt(1), false)};
		TS_ASSERT_DELTA(moving.X.Absolute().ToDouble(), 1.0, 0.001);

		// Without inertia, speed doesn't matter.
		const CFixedVector2D agile{PathFollowing::SteeringVelocity(side, HeadingControl::FacingDirection(fixed::Zero()), maxSpeed, maxSpeed,
			fixed::Zero(), fixed::FromInt(1), false)};
		TS_ASSERT_DELTA(agile.X.Absolute().ToDouble(), 10.0, 0.001);
	}

	void test_FinalWaypoint()
	{
		const WaypointPath none;
		TS_ASSERT(!PathFollowing::FinalWaypoint(none, none));
		// The long path's end, even while following a short path.
		TS_ASSERT_EQUALS(*PathFollowing::FinalWaypoint(MakePath({4, 3}), MakePath({2, 1})), CFixedVector2D(fixed::FromInt(2), fixed::Zero()));
		TS_ASSERT_EQUALS(*PathFollowing::FinalWaypoint(MakePath({4, 3}), none), CFixedVector2D(fixed::FromInt(4), fixed::Zero()));
	}

	void test_StoppingDistance()
	{
		TS_ASSERT_EQUALS(PathFollowing::StoppingDistance(fixed::FromInt(10), fixed::FromInt(1), fixed::Zero()), fixed::FromInt(10));
		TS_ASSERT_EQUALS(PathFollowing::StoppingDistance(fixed::FromInt(10), fixed::FromInt(1), fixed::FromInt(2)), fixed::FromInt(30));
	}

	void test_Alignment()
	{
		const CFixedVector2D north{fixed::Zero(), fixed::FromInt(3)};
		TS_ASSERT_DELTA(PathFollowing::Alignment(north, CFixedVector2D(fixed::Zero(), fixed::FromInt(7))).ToDouble(), 1.0, 0.001);
		TS_ASSERT_EQUALS(PathFollowing::Alignment(north, CFixedVector2D(fixed::FromInt(5), fixed::Zero())), fixed::Zero());
		// Pointing apart counts as no alignment, not negative.
		TS_ASSERT_EQUALS(PathFollowing::Alignment(north, CFixedVector2D(fixed::Zero(), -fixed::FromInt(1))), fixed::Zero());
	}

	void test_TurnSpeedCap()
	{
		const fixed maxSpeed{fixed::FromInt(10)};
		const entity_angle_t rightAngle{entity_angle_t::Pi() / 2};

		TS_ASSERT(!PathFollowing::TurnSpeedCap(fixed::Zero(), fixed::FromFraction(1, 2), maxSpeed));
		TS_ASSERT(!PathFollowing::TurnSpeedCap(rightAngle, fixed::Zero(), maxSpeed));

		// The turn braking is the share of top speed given up at a right angle...
		TS_ASSERT_EQUALS(*PathFollowing::TurnSpeedCap(rightAngle, fixed::FromFraction(1, 2), maxSpeed), fixed::FromInt(5));
		// ...or sharper.
		TS_ASSERT_EQUALS(*PathFollowing::TurnSpeedCap(entity_angle_t::Pi(), fixed::FromFraction(1, 2), maxSpeed), fixed::FromInt(5));
		// A half right angle gives up half as much.
		TS_ASSERT_DELTA(PathFollowing::TurnSpeedCap(rightAngle / 2, fixed::FromFraction(1, 2), maxSpeed)->ToDouble(), 7.5, 0.001);
		// A turn braking of 1 stops to turn.
		TS_ASSERT_EQUALS(*PathFollowing::TurnSpeedCap(rightAngle, fixed::FromInt(1), maxSpeed), fixed::Zero());
	}

	void test_ProximityBrakeScale()
	{
		const fixed brakingDistance{fixed::FromInt(10)};
		const fixed speed{fixed::FromInt(5)};

		// Well aligned: no need to brake, even at the waypoint.
		TS_ASSERT_DELTA(PathFollowing::ProximityBrakeScale(fixed::Zero(), brakingDistance, speed, fixed::FromInt(1)).ToDouble(), 1.0, 0.001);

		// Badly aligned halfway in: brakes, but never below the speed a constant deceleration would still have
		// there, the square root of the share of the braking distance left, so it doesn't stop short of the
		// waypoint.
		const fixed halfway{PathFollowing::ProximityBrakeScale(fixed::FromInt(5), brakingDistance, speed, fixed::Zero())};
		TS_ASSERT_DELTA(halfway.ToDouble(), std::sqrt(0.5), 0.001);

		// The worse the alignment, the harder it brakes.
		const fixed quarterIn{PathFollowing::ProximityBrakeScale(fixed::FromInt(8), brakingDistance, speed, fixed::FromFraction(9, 10))};
		const fixed quarterInBadly{PathFollowing::ProximityBrakeScale(fixed::FromInt(8), brakingDistance, speed, fixed::FromFraction(1, 10))};
		TS_ASSERT_LESS_THAN(quarterInBadly, quarterIn);
		TS_ASSERT_LESS_THAN_EQUALS(quarterIn, fixed::FromInt(1));

		// On top of a waypoint that's behind us: brakes as hard as it can, but never to a standstill,
		// which PerformMove would take for an obstruction.
		const fixed onTopBehind{PathFollowing::ProximityBrakeScale(fixed::Zero(), brakingDistance, speed, fixed::Zero())};
		TS_ASSERT_LESS_THAN(fixed::Zero(), onTopBehind);
		TS_ASSERT_LESS_THAN(onTopBehind, fixed::FromFraction(1, 100));
		TS_ASSERT_LESS_THAN(fixed::Zero(), speed.Multiply(onTopBehind));

		// Already slower than that crawl: never sped up.
		TS_ASSERT_EQUALS(PathFollowing::ProximityBrakeScale(fixed::Zero(), brakingDistance, fixed::Epsilon(), fixed::Zero()),
			fixed::FromInt(1));
	}

	void test_PredictiveBrakeFactor()
	{
		// A unit that stops from 10 m/s over 10 m brakes at 5 m/s².
		const fixed maxSpeed{fixed::FromInt(10)};
		const fixed stoppingDistance{fixed::FromInt(10)};
		const fixed dt{fixed::FromFraction(1, 5)};

		// Beyond the stopping distance, or standing still: nothing to brake for.
		TS_ASSERT(!PathFollowing::PredictiveBrakeFactor(fixed::FromInt(10), maxSpeed, maxSpeed, stoppingDistance, dt));
		TS_ASSERT(!PathFollowing::PredictiveBrakeFactor(fixed::FromInt(5), fixed::Zero(), maxSpeed, stoppingDistance, dt));

		// A quarter of the distance left: half the speed, sqrt(2 * 5 * 2.5).
		TS_ASSERT_DELTA(PathFollowing::PredictiveBrakeFactor(fixed::FromFraction(5, 2), maxSpeed, maxSpeed, stoppingDistance, dt)->ToDouble(),
			0.5, 0.001);
		// Already slower than that: left alone.
		TS_ASSERT(!PathFollowing::PredictiveBrakeFactor(fixed::FromFraction(5, 2), fixed::FromInt(4), maxSpeed, stoppingDistance, dt));

		// Close to the end the step is cut to land on it, not past it.
		const fixed nearEnd{fixed::FromFraction(1, 10)};
		const fixed factor{*PathFollowing::PredictiveBrakeFactor(nearEnd, maxSpeed, maxSpeed, stoppingDistance, dt)};
		TS_ASSERT_LESS_THAN_EQUALS(maxSpeed.Multiply(factor).Multiply(dt), nearEnd);

		// On the end itself, a crawl rather than a standstill, which PerformMove would take for an obstruction.
		TS_ASSERT_LESS_THAN(fixed::Zero(), *PathFollowing::PredictiveBrakeFactor(fixed::Zero(), maxSpeed, maxSpeed, stoppingDistance, dt));

		// From full speed at the stopping distance, the unit reaches the end in about the 2 s a constant
		// deceleration takes, instead of creeping towards it.
		fixed distance{stoppingDistance};
		fixed speed{maxSpeed};
		int turns{0};
		for (; turns < 50 && distance > fixed::Zero(); ++turns)
		{
			if (const std::optional<fixed> brake{PathFollowing::PredictiveBrakeFactor(distance, speed, maxSpeed, stoppingDistance, dt)})
				speed = speed.Multiply(*brake);
			distance = std::max(distance - speed.Multiply(dt), fixed::Zero());
		}
		TS_ASSERT_EQUALS(distance, fixed::Zero());
		TS_ASSERT_LESS_THAN_EQUALS(turns, 14);
	}

	void test_AcceptanceRadius()
	{
		const CFixedVector2D pos;
		const fixed maxSpeed{fixed::FromInt(10)};
		const fixed dt{fixed::FromInt(1)};
		const fixed inertia{fixed::FromInt(1)};
		const CFixedVector2D waypoint{fixed::Zero(), fixed::FromInt(15)};
		// The caller now passes these in rather than having them recomputed; they are what it would have measured.
		const fixed toWaypoint{(waypoint - pos).Length()};

		// Standing still: at least a navcell.
		TS_ASSERT_EQUALS(PathFollowing::AcceptanceRadius(pos, waypoint, std::nullopt, CFixedVector2D{}, fixed::Zero(),
			toWaypoint, fixed::Zero(), maxSpeed, inertia, fixed::Zero(), dt), Pathfinding::NAVCELL_SIZE);

		// Not moving this turn, but carried at 5 by momentum: the stopping distance, 5·1·(1 + 1).
		TS_ASSERT_EQUALS(PathFollowing::AcceptanceRadius(pos, waypoint, std::nullopt, CFixedVector2D{}, fixed::Zero(),
			toWaypoint, fixed::FromInt(5), maxSpeed, inertia, fixed::Zero(), dt), fixed::FromInt(10));

		// Never more than the stopping distance at max speed.
		TS_ASSERT_EQUALS(PathFollowing::AcceptanceRadius(pos, waypoint, std::nullopt, CFixedVector2D{}, fixed::Zero(),
			toWaypoint, fixed::FromInt(50), maxSpeed, inertia, fixed::Zero(), dt), fixed::FromInt(20));

		// Heading straight for the waypoint shrinks it, so corners aren't cut; heading sideways doesn't.
		const CFixedVector2D towards{fixed::Zero(), fixed::FromInt(5)};
		const CFixedVector2D sideways{fixed::FromInt(5), fixed::Zero()};
		const fixed straight{PathFollowing::AcceptanceRadius(pos, waypoint, std::nullopt, towards, towards.Length(),
			toWaypoint, fixed::FromInt(5), maxSpeed, inertia, fixed::Zero(), dt)};
		TS_ASSERT_DELTA(straight.ToDouble(), 6.0, 0.001);
		TS_ASSERT_EQUALS(PathFollowing::AcceptanceRadius(pos, waypoint, std::nullopt, sideways, sideways.Length(),
			toWaypoint, fixed::FromInt(5), maxSpeed, inertia, fixed::Zero(), dt), fixed::FromInt(10));

		// Closer to the next waypoint than to this one: larger again.
		const CFixedVector2D nextWaypoint{fixed::FromInt(2), fixed::Zero()};
		TS_ASSERT_LESS_THAN(straight, PathFollowing::AcceptanceRadius(pos, waypoint, nextWaypoint, towards, towards.Length(),
			toWaypoint, fixed::FromInt(5), maxSpeed, inertia, fixed::Zero(), dt));

		// More drift correction: smaller.
		TS_ASSERT_LESS_THAN(PathFollowing::AcceptanceRadius(pos, waypoint, std::nullopt, towards, towards.Length(),
			toWaypoint, fixed::FromInt(5), maxSpeed, inertia, fixed::FromInt(1), dt), straight);
	}

	void test_PassedWaypoint()
	{
		// A leg north from the waypoint at (0, 10) to the next one at (0, 20).
		const CFixedVector2D waypoint{fixed::Zero(), fixed::FromInt(10)};
		const CFixedVector2D next{fixed::Zero(), fixed::FromInt(20)};
		// Short of it: not passed.
		TS_ASSERT(!PathFollowing::PassedWaypoint(CFixedVector2D{fixed::Zero(), fixed::FromInt(5)}, waypoint, next));
		// Pushed beyond it, even well off to the side: passed.
		TS_ASSERT(PathFollowing::PassedWaypoint(CFixedVector2D{fixed::Zero(), fixed::FromInt(12)}, waypoint, next));
		TS_ASSERT(PathFollowing::PassedWaypoint(CFixedVector2D{fixed::FromInt(6), fixed::FromInt(11)}, waypoint, next));
		// Level with it, or beside it: not yet.
		TS_ASSERT(!PathFollowing::PassedWaypoint(CFixedVector2D{fixed::FromInt(6), fixed::FromInt(10)}, waypoint, next));
	}

	void test_CannotReach()
	{
		const CFixedVector2D pos;
		const CFixedVector2D velocity{fixed::Zero(), fixed::FromInt(10)};
		const fixed speed{velocity.Length()};
		const fixed turnRate{fixed::FromInt(1)};
		const fixed maxDistance{fixed::FromInt(100)};

		// Straight ahead is always reachable.
		const CFixedVector2D ahead{fixed::Zero(), fixed::FromInt(5)};
		TS_ASSERT(!PathFollowing::CannotReach(pos, ahead, velocity, speed, ahead.Length(), fixed::Zero(),
			turnRate, fixed::FromInt(1), maxDistance));

		// Close beside a fast unit that turns slowly: inside its turning circle.
		const CFixedVector2D beside{fixed::FromInt(3), fixed::FromInt(1)};
		const fixed besideDistance{beside.Length()};
		TS_ASSERT(PathFollowing::CannotReach(pos, beside, velocity, speed, besideDistance, fixed::Zero(), turnRate,
			fixed::FromInt(1), maxDistance));

		// ...unless it turns fast enough.
		TS_ASSERT(!PathFollowing::CannotReach(pos, beside, velocity, speed, besideDistance, fixed::Zero(),
			fixed::FromInt(100), fixed::FromInt(100), maxDistance));

		// A high turn rate is no use if the unit turns up too slowly to reach it in time.
		TS_ASSERT(PathFollowing::CannotReach(pos, beside, velocity, speed, besideDistance, fixed::Zero(),
			fixed::FromInt(100), fixed::FromInt(1), maxDistance));

		// Not asked beyond the stopping distance, nor when standing still.
		TS_ASSERT(!PathFollowing::CannotReach(pos, beside, velocity, speed, besideDistance, fixed::Zero(), turnRate,
			fixed::FromInt(1), fixed::FromInt(2)));
		TS_ASSERT(!PathFollowing::CannotReach(pos, beside, CFixedVector2D{}, fixed::Zero(), besideDistance, fixed::Zero(),
			turnRate, fixed::FromInt(1), maxDistance));
	}

	void test_NearestPointOnSegment()
	{
		const CFixedVector2D a{fixed::FromInt(10), fixed::FromInt(10)};
		const CFixedVector2D b{fixed::FromInt(20), fixed::FromInt(10)};

		// Before, after and beside the segment.
		TS_ASSERT_EQUALS(Geometry::NearestPointOnSegment(CFixedVector2D{fixed::Zero(), fixed::Zero()}, a, b), a);
		TS_ASSERT_EQUALS(Geometry::NearestPointOnSegment(CFixedVector2D{fixed::FromInt(50), fixed::FromInt(10)}, a, b), b);
		TS_ASSERT_EQUALS(Geometry::NearestPointOnSegment(CFixedVector2D{fixed::FromInt(15), fixed::FromInt(40)}, a, b),
			CFixedVector2D(fixed::FromInt(15), fixed::FromInt(10)));

		// A zero-length segment is its own start point.
		TS_ASSERT_EQUALS(Geometry::NearestPointOnSegment(CFixedVector2D{fixed::FromInt(15), fixed::FromInt(40)}, a, a), a);
	}

	void test_DistanceSegmentToSegment()
	{
		CFixedVector2D nearA, nearB;

		// Parallel, 4 m apart.
		TS_ASSERT_EQUALS(Geometry::DistanceSegmentToSegment(
			CFixedVector2D{fixed::Zero(), fixed::Zero()}, CFixedVector2D{fixed::FromInt(10), fixed::Zero()},
			CFixedVector2D{fixed::Zero(), fixed::FromInt(4)}, CFixedVector2D{fixed::FromInt(10), fixed::FromInt(4)},
			nearA, nearB), fixed::FromInt(4));

		// Crossing: zero, at the crossing point.
		TS_ASSERT_EQUALS(Geometry::DistanceSegmentToSegment(
			CFixedVector2D{fixed::Zero(), fixed::Zero()}, CFixedVector2D{fixed::FromInt(10), fixed::FromInt(10)},
			CFixedVector2D{fixed::Zero(), fixed::FromInt(10)}, CFixedVector2D{fixed::FromInt(10), fixed::Zero()},
			nearA, nearB), fixed::Zero());
		TS_ASSERT_EQUALS(nearA, nearB);
		TS_ASSERT_DELTA(nearA.X.ToDouble(), 5.0, 0.01);
		TS_ASSERT_DELTA(nearA.Y.ToDouble(), 5.0, 0.01);

		// T-shaped, stopping 3 m short.
		TS_ASSERT_EQUALS(Geometry::DistanceSegmentToSegment(
			CFixedVector2D{fixed::Zero(), fixed::Zero()}, CFixedVector2D{fixed::FromInt(10), fixed::Zero()},
			CFixedVector2D{fixed::FromInt(5), fixed::FromInt(3)}, CFixedVector2D{fixed::FromInt(5), fixed::FromInt(9)},
			nearA, nearB), fixed::FromInt(3));

		// Collinear and overlapping: touching.
		TS_ASSERT_EQUALS(Geometry::DistanceSegmentToSegment(
			CFixedVector2D{fixed::Zero(), fixed::Zero()}, CFixedVector2D{fixed::FromInt(10), fixed::Zero()},
			CFixedVector2D{fixed::FromInt(5), fixed::Zero()}, CFixedVector2D{fixed::FromInt(15), fixed::Zero()},
			nearA, nearB), fixed::Zero());

		// Both degenerate: plain point to point.
		const CFixedVector2D p{fixed::FromInt(3), fixed::FromInt(4)};
		TS_ASSERT_EQUALS(Geometry::DistanceSegmentToSegment(CFixedVector2D{}, CFixedVector2D{}, p, p, nearA, nearB),
			fixed::FromInt(5));

		// One degenerate: the same as point to segment.
		const CFixedVector2D a0{fixed::FromInt(1), fixed::FromInt(1)};
		const CFixedVector2D a1{fixed::FromInt(9), fixed::FromInt(1)};
		TS_ASSERT_EQUALS(Geometry::DistanceSegmentToSegment(a0, a1, p, p, nearA, nearB),
			Geometry::DistanceToSegment(p, a0, a1));
	}

	void test_DistanceSegmentToSegment_meets_parallel_segments_in_the_middle()
	{
		CFixedVector2D nearA, nearB;

		// Two segments lying alongside each other are equally close everywhere they overlap. Callers
		// take a lever arm from these points, so the answer has to be the middle of that overlap: an end
		// of it would read as an off-centre contact and spin a body that is being shoved square on.
		Geometry::DistanceSegmentToSegment(
			CFixedVector2D{fixed::Zero(), fixed::FromInt(-15)}, CFixedVector2D{fixed::Zero(), fixed::FromInt(15)},
			CFixedVector2D{fixed::FromInt(8), fixed::FromInt(-15)}, CFixedVector2D{fixed::FromInt(8), fixed::FromInt(15)},
			nearA, nearB);
		TS_ASSERT_EQUALS(nearA, CFixedVector2D(fixed::Zero(), fixed::Zero()));
		TS_ASSERT_EQUALS(nearB, CFixedVector2D(fixed::FromInt(8), fixed::Zero()));

		// Offset along their length, the overlap runs from 0 to 15 and its middle is 7.5.
		Geometry::DistanceSegmentToSegment(
			CFixedVector2D{fixed::Zero(), fixed::FromInt(-15)}, CFixedVector2D{fixed::Zero(), fixed::FromInt(15)},
			CFixedVector2D{fixed::FromInt(8), fixed::Zero()}, CFixedVector2D{fixed::FromInt(8), fixed::FromInt(30)},
			nearA, nearB);
		TS_ASSERT_EQUALS(nearA.Y, fixed::FromFraction(15, 2));
		TS_ASSERT_EQUALS(nearB.Y, fixed::FromFraction(15, 2));
	}

	void test_DeriveCollisionShape_keeps_round_footprints_round()
	{
		// A square footprint has no direction to stretch along: a circle of half its side.
		const UnitCollision::CollisionShape square{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(12))};
		TS_ASSERT(square.IsCircle());
		TS_ASSERT_EQUALS(square.radius, entity_pos_t::FromInt(6));
		TS_ASSERT_EQUALS(square.halfLength, entity_pos_t::Zero());

		// A circular footprint of radius 4 is given as its diameter both ways, and comes back as itself.
		const UnitCollision::CollisionShape circle{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(8), entity_pos_t::FromInt(8))};
		TS_ASSERT(circle.IsCircle());
		TS_ASSERT_EQUALS(circle.radius, entity_pos_t::FromInt(4));

		// A body with no size at all - a target without a PhysicalBody - is a point, and so a circle: the
		// rules for solid units must not apply to it.
		const UnitCollision::CollisionShape point{entity_pos_t::Zero(), entity_pos_t::Zero(), false, false};
		TS_ASSERT(point.IsCircle());
	}

	void test_DeriveCollisionShape_is_the_footprint()
	{
		// A warship's 12 x 48 footprint: the body is that outline exactly, whatever the 10 m ship clearance
		// says. That clearance is a shore margin for the pathfinder and would give this ship, and every
		// smaller one, the same 10 x 40 body.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		TS_ASSERT(!ship.IsCircle());
		TS_ASSERT_EQUALS(ship.radius, entity_pos_t::FromInt(6));
		TS_ASSERT_EQUALS(ship.halfLength, entity_pos_t::FromInt(18));
		TS_ASSERT_EQUALS(ship.Extent(), entity_pos_t::FromInt(24));

		// A fishing boat, 4 x 10, is its own small outline too: there is no size a footprint has to reach.
		const UnitCollision::CollisionShape fishing{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(4), entity_pos_t::FromInt(10))};
		TS_ASSERT(!fishing.IsCircle());
		TS_ASSERT_EQUALS(fishing.radius, entity_pos_t::FromInt(2));
		TS_ASSERT_EQUALS(fishing.halfLength, entity_pos_t::FromInt(3));
	}

	void test_DeriveCollisionShape_runs_along_the_longer_side()
	{
		// Deeper than wide: the body runs along the entity's forward axis.
		TS_ASSERT(!UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(3), entity_pos_t::FromInt(6)).alongWidth);
		// Wider than deep: it runs across instead, and is the same shape.
		const UnitCollision::CollisionShape wide{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(6), entity_pos_t::FromInt(3))};
		TS_ASSERT(wide.alongWidth);
		TS_ASSERT_EQUALS(wide.radius,
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(3), entity_pos_t::FromInt(6)).radius);
		TS_ASSERT_EQUALS(wide.halfLength,
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(3), entity_pos_t::FromInt(6)).halfLength);
	}

	void test_CollisionShape_segment_follows_the_facing()
	{
		const CFixedVector2D position{fixed::FromInt(100), fixed::FromInt(100)};
		const UnitCollision::CollisionShape body{entity_pos_t::FromInt(5), entity_pos_t::FromInt(15), false};
		CFixedVector2D start, end;

		// Facing 0 is along +Z, so the body runs north-south.
		body.GetSegment(position, entity_angle_t::Zero(), start, end);
		TS_ASSERT_DELTA(start.X.ToDouble(), 100.0, 0.01);
		TS_ASSERT_DELTA(start.Y.ToDouble(), 85.0, 0.01);
		TS_ASSERT_DELTA(end.Y.ToDouble(), 115.0, 0.01);

		// Turned a quarter circle, it runs east-west.
		body.GetSegment(position, fixed::Pi() / 2, start, end);
		TS_ASSERT_DELTA(start.X.ToDouble(), 85.0, 0.05);
		TS_ASSERT_DELTA(start.Y.ToDouble(), 100.0, 0.05);

		// A circular body has no direction to follow.
		const UnitCollision::CollisionShape circle{entity_pos_t::FromInt(5), entity_pos_t::Zero(), false, false};
		circle.GetSegment(position, fixed::Pi() / 2, start, end);
		TS_ASSERT_EQUALS(start, position);
		TS_ASSERT_EQUALS(end, position);
	}

	// A moving unit of its own, for the pushing tests below.
	static UnitCollision::MotionStateCompact MakeUnit(entity_id_t entity, const CFixedVector2D& position,
		const UnitCollision::CollisionShape& body, entity_angle_t angle = entity_angle_t::Zero(), bool moving = true,
		const CFixedVector2D& velocity = CFixedVector2D{})
	{
		UnitCollision::MotionStateCompact unit{};
		unit.entity = entity;
		unit.position = position;
		unit.angle = angle;
		unit.clearance = body.radius;
		unit.collisionShape = body;
		unit.weight = entity_pos_t::FromInt(10);
		unit.isMoving = moving;
		unit.ignore = false;
		unit.controlGroup = INVALID_ENTITY;
		unit.velocity = velocity;
		unit.charging = false;
		unit.minChargeSpeed = entity_pos_t::Zero();
		unit.passClass = 1;
		return unit;
	}

	// Everything a pair exchanges in one turn: pushes, push spins, contact impulses, hit spins and charge hits.
	static void PushPair(const UnitCollision::MotionStateCompact& a, const UnitCollision::MotionStateCompact& b,
		std::vector<CFixedVector2D>& pushes, std::vector<fixed>& spins, std::vector<CFixedVector2D>& velocityChanges,
		std::vector<UnitCollision::ContactHit>& hits)
	{
		UnitCollision::SpatialGrids grids;
		std::vector<fixed> hitSpins;
		std::vector<uint8_t> pressures;

		std::vector<CFixedVector2D> corrections;
		UnitCollision::ComputePushes({a, b}, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
			velocityChanges, hitSpins, hits, pressures, corrections);
	}

	static void PushPair(const UnitCollision::MotionStateCompact& a, const UnitCollision::MotionStateCompact& b,
		std::vector<CFixedVector2D>& pushes, std::vector<fixed>& spins, std::vector<CFixedVector2D>& velocityChanges)
	{
		std::vector<UnitCollision::ContactHit> hits;
		PushPair(a, b, pushes, spins, velocityChanges, hits);
	}

	static void PushPair(const UnitCollision::MotionStateCompact& a, const UnitCollision::MotionStateCompact& b,
		std::vector<CFixedVector2D>& pushes, std::vector<fixed>& spins)
	{
		std::vector<CFixedVector2D> velocityChanges;
		PushPair(a, b, pushes, spins, velocityChanges);
	}

	static bool PushesApart(const UnitCollision::MotionStateCompact& a, const UnitCollision::MotionStateCompact& b)
	{
		std::vector<CFixedVector2D> pushes;
		std::vector<fixed> spins;
		PushPair(a, b, pushes, spins);

		return pushes.size() == 2 && !pushes[0].IsZero();
	}

	// How far apart two identical units can be and still push, found by bisection rather than by
	// repeating ComputePairPush's arithmetic here.
	static double PushReach(const UnitCollision::CollisionShape& body, entity_angle_t angle, bool alongZ)
	{
		double low = 0.0, high = 200.0;
		for (int i = 0; i < 24; ++i)
		{
			const double mid{(low + high) / 2};
			const fixed offset{fixed::FromDouble(mid)};
			const CFixedVector2D other{alongZ ? CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100) + offset}
			                                  : CFixedVector2D{fixed::FromInt(100) + offset, fixed::FromInt(100)}};
			if (PushesApart(MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, body, angle),
			                MakeUnit(2, other, body, angle)))
				low = mid;
			else
				high = mid;
		}
		return low;
	}

	void test_Pushing_circles_reach_exactly_as_far_as_before()
	{
		// The distance a pair of circles push over is unchanged by bodies: combined radius, corrected and
		// scaled, plus the moving extension. Two circular bodies must still be a plain centre-to-centre pair.
		const entity_pos_t clearance{entity_pos_t::FromFraction(8, 10)};
		const UnitCollision::CollisionShape circle{clearance, entity_pos_t::Zero(), false, false};

		const PushConfig::RuntimeConfig& cfg{PushConfig::GetInstance().GetRuntimeConfig()};
		const double expected{((clearance + clearance).Multiply(PushConfig::PUSHING_CORRECTION)
			.Multiply(cfg.radiusMultiplier) + cfg.movingPushExtension).ToDouble()};

		TS_ASSERT_DELTA(PushReach(circle, entity_angle_t::Zero(), true), expected, 0.01);
		// And it is the same in every direction.
		TS_ASSERT_DELTA(PushReach(circle, entity_angle_t::Zero(), false), expected, 0.01);
	}

	void test_Pushing_solid_bodies_reach_further_along_their_length()
	{
		// A warship body, its 12 x 48 footprint: 6 m of beam, 18 m of hull either side of the centre.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};

		// Facing 0, so the ships run along Z. Line astern they push from much further apart than abreast.
		const double astern{PushReach(ship, entity_angle_t::Zero(), true)};
		const double abreast{PushReach(ship, entity_angle_t::Zero(), false)};
		TS_ASSERT_DELTA(astern - abreast, (ship.halfLength * 2).ToDouble(), 0.05);
		TS_ASSERT_LESS_THAN(abreast * 2, astern);

		// Turning both ships a quarter circle swaps the two.
		const fixed quarter{fixed::Pi() / 2};
		TS_ASSERT_DELTA(PushReach(ship, quarter, false), astern, 0.1);
		TS_ASSERT_DELTA(PushReach(ship, quarter, true), abreast, 0.1);

		// Against the 10 m clearance circle these ships used to be: tighter broadside, longer fore and aft.
		const UnitCollision::CollisionShape disc{entity_pos_t::FromInt(10), entity_pos_t::Zero(), false, false};
		const double circleReach{PushReach(disc, entity_angle_t::Zero(), false)};
		TS_ASSERT_LESS_THAN(abreast, circleReach);
		TS_ASSERT_LESS_THAN(circleReach, astern);
	}
	void test_Pushing_visits_every_pair_across_cells()
	{
		// The pass sweeps the spatial grid cell by cell and takes each pair of cells one way round, and the
		// large bodies search the cells around them. Rows of units a metre apart, each row longer than a
		// cell, straddle cell boundaries along x, along z and on both diagonals; what each unit ends up
		// pushed by must be the sum of what every pair gives it on its own.
		const UnitCollision::CollisionShape circle{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		std::vector<UnitCollision::MotionStateCompact> units;
		for (int k = 0; k < 16; ++k)
		{
			const fixed step{fixed::FromInt(k)};
			const auto add = [&](const CFixedVector2D& position) {
				units.push_back(MakeUnit(static_cast<entity_id_t>(units.size() + 1), position, circle));
			};
			add(CFixedVector2D{fixed::FromInt(100) + step, fixed::FromInt(100)});
			add(CFixedVector2D{fixed::FromInt(130), fixed::FromInt(100) + step});
			add(CFixedVector2D{fixed::FromInt(150) + step, fixed::FromInt(100) + step});
			add(CFixedVector2D{fixed::FromInt(170) + step, fixed::FromInt(115) - step});
			// A column of footmen along a warship's side: the ship is tiers above them and has to find them
			// across several of their cells by itself.
			add(CFixedVector2D{fixed::FromInt(208), fixed::FromInt(92) + step});
		}
		// Two warships (12 x 48, so a reach of 26 m) abreast, and an elephant-sized circle beside the first:
		// three tiers, each pair of tiers found by the higher one scanning the lower one's grid.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const UnitCollision::CollisionShape elephant{entity_pos_t::FromInt(3), entity_pos_t::Zero(), false, false};
		units.push_back(MakeUnit(static_cast<entity_id_t>(units.size() + 1), CFixedVector2D{fixed::FromInt(200), fixed::FromInt(100)}, ship));
		units.push_back(MakeUnit(static_cast<entity_id_t>(units.size() + 1), CFixedVector2D{fixed::FromInt(215), fixed::FromInt(100)}, ship));
		units.push_back(MakeUnit(static_cast<entity_id_t>(units.size() + 1), CFixedVector2D{fixed::FromInt(192), fixed::FromInt(125)}, elephant));

		UnitCollision::SpatialGrids grids;
		std::vector<CFixedVector2D> pushes, velocityChanges;
		std::vector<fixed> spins, hitSpins;
		std::vector<UnitCollision::ContactHit> hits;
		std::vector<uint8_t> pressures;
		std::vector<CFixedVector2D> corrections;
		UnitCollision::ComputePushes(units, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
			velocityChanges, hitSpins, hits, pressures, corrections);
		// Footmen, the elephant and the ships each fall in their own tier.
		TS_ASSERT_EQUALS(grids.tiers.size(), static_cast<size_t>(4));
		TS_ASSERT(grids.tiers[0].used && grids.tiers[1].used && !grids.tiers[2].used && grids.tiers[3].used);

		std::vector<CFixedVector2D> expected(units.size());
		std::vector<fixed> largest(units.size());
		size_t pushingPairs{0};
		for (size_t i = 0; i < units.size(); ++i)
			for (size_t j = i + 1; j < units.size(); ++j)
			{
				std::vector<CFixedVector2D> pairPushes;
				std::vector<fixed> pairSpins;
				PushPair(units[i], units[j], pairPushes, pairSpins);
				// Between two solid bodies, the pass scales each push by the room where it leads.
				if (units[i].collisionShape.IsSolid() && units[j].collisionShape.IsSolid())
					for (const auto& [pushed, pusher, k] : {std::tuple{i, j, 0}, std::tuple{j, i, 1}})
						if (!pairPushes[k].IsZero())
						{
							CFixedVector2D direction{pairPushes[k]};
							direction.Normalize();
							pairPushes[k] = pairPushes[k].Multiply(UnitCollision::PushRoom(grids.density, units, pushed, pusher,
								direction, PushConfig::GetInstance().GetRuntimeConfig().densityPushCoverage,
								PushConfig::GetInstance().GetRuntimeConfig().densityPushFloor, nullptr));
						}
				expected[i] += pairPushes[0];
				expected[j] += pairPushes[1];
				largest[i] = std::max(largest[i], pairPushes[0].Length());
				largest[j] = std::max(largest[j], pairPushes[1].Length());
				if (!pairPushes[0].IsZero())
					++pushingPairs;
			}
		// The ships are solid, so their sum is capped at their largest single push.
		for (size_t i = 0; i < units.size(); ++i)
			if (units[i].collisionShape.IsSolid() && largest[i] > fixed::Zero() && expected[i].CompareLength(largest[i]) > 0)
				expected[i] = expected[i].Multiply(largest[i] / expected[i].Length());
		// Neighbours along every row push each other, so this is not a test that nothing happens.
		TS_ASSERT_LESS_THAN(static_cast<size_t>(50), pushingPairs);
		for (size_t i = 0; i < units.size(); ++i)
		{
			TS_ASSERT_EQUALS(pushes[i].X, expected[i].X);
			TS_ASSERT_EQUALS(pushes[i].Y, expected[i].Y);
		}
	}

	void test_InertiaPerMass()
	{
		// A round body: (r^2 + r^2) / 3.
		const UnitCollision::CollisionShape circle{entity_pos_t::FromInt(3), entity_pos_t::Zero(), false, false};
		TS_ASSERT_DELTA(circle.InertiaPerMass().ToDouble(), (9.0 + 9.0) / 3, 0.01);

		// A 12 x 48 warship: radius 6, extent 24.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		TS_ASSERT_DELTA(ship.InertiaPerMass().ToDouble(), (36.0 + 576.0) / 3, 0.05);

		// The same size as a circle, but far harder to spin.
		const UnitCollision::CollisionShape disc{entity_pos_t::FromInt(10), entity_pos_t::Zero(), false, false};
		TS_ASSERT_LESS_THAN(disc.InertiaPerMass(), ship.InertiaPerMass());
	}

	void test_SpinFromVelocityChange()
	{
		const entity_pos_t inertia{entity_pos_t::FromInt(100)};
		const CFixedVector2D sideways{fixed::FromInt(4), fixed::Zero()};

		// Struck through the centre: no lever, no spin, however hard.
		TS_ASSERT_EQUALS(UnitCollision::SpinFromVelocityChange(CFixedVector2D{}, sideways, inertia), fixed::Zero());

		// Struck off one end, then the other: opposite spins of the same size.
		const CFixedVector2D bow{fixed::Zero(), fixed::FromInt(20)};
		const fixed atBow{UnitCollision::SpinFromVelocityChange(bow, sideways, inertia)};
		TS_ASSERT_DIFFERS(atBow, fixed::Zero());
		TS_ASSERT_EQUALS(UnitCollision::SpinFromVelocityChange(-bow, sideways, inertia), -atBow);

		// And in the right direction. The bow, out along +Z, is shoved towards +X, so the heading has to
		// swing from +Z towards +X: a positive entity angle (see CFixedVector2D::Rotate), which is the
		// opposite sign from a textbook cross product. This is the case that is wrong when the ships
		// spin away from the blow instead of with it.
		TS_ASSERT_LESS_THAN(fixed::Zero(), atBow);
		CFixedVector2D start, end;
		const UnitCollision::CollisionShape longBody{entity_pos_t::FromInt(5), entity_pos_t::FromInt(20), false, true};
		longBody.GetSegment(CFixedVector2D{}, atBow.Multiply(fixed::FromFraction(1, 10)), start, end);
		TS_ASSERT_LESS_THAN(fixed::Zero(), end.X);

		// cross(lever, velocity) / inertia: 20 * 4 / 100.
		TS_ASSERT_DELTA(atBow.Absolute().ToDouble(), 20.0 * 4 / 100, 0.01);

		// A push along the lever slides the body without turning it.
		TS_ASSERT_EQUALS(UnitCollision::SpinFromVelocityChange(bow, CFixedVector2D{fixed::Zero(), fixed::FromInt(4)},
			inertia), fixed::Zero());

		// A heavier body to turn spins less for the same blow.
		TS_ASSERT_LESS_THAN(
			UnitCollision::SpinFromVelocityChange(bow, sideways, inertia * 2).Absolute(), atBow.Absolute());

		// Zero inertia is meaningless rather than infinite: it must not divide by it.
		TS_ASSERT_EQUALS(UnitCollision::SpinFromVelocityChange(bow, sideways, entity_pos_t::Zero()), fixed::Zero());
	}

	void test_Pushing_circles_never_spin()
	{
		// The guard on the whole land game: a round body is always pushed through its own centre, so
		// adding rotation to pushing must leave every unit with a Circle footprint exactly as it was.
		const UnitCollision::CollisionShape circle{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};

		std::vector<CFixedVector2D> pushes;
		std::vector<fixed> spins;
		// Offset on both axes, so neither is a special case.
		PushPair(MakeUnit(1, here, circle),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(1), fixed::FromInt(1)}, circle), pushes, spins);

		TS_ASSERT(!pushes[0].IsZero());
		TS_ASSERT_EQUALS(spins[0], fixed::Zero());
		TS_ASSERT_EQUALS(spins[1], fixed::Zero());
	}

	void test_Pushing_solid_bodies_spin_when_shoved_off_centre()
	{
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};

		std::vector<CFixedVector2D> pushes;
		std::vector<fixed> spins;

		// Both ships run along Z. Abreast and level, each is shoved square on its middle: the nearest
		// points are the two centres, so there is no lever and no spin.
		PushPair(MakeUnit(1, here, ship),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(8), fixed::Zero()}, ship), pushes, spins);
		TS_ASSERT(!pushes[0].IsZero());
		TS_ASSERT_EQUALS(spins[0], fixed::Zero());

		// Staggered along Z by 10 m, the solid bodies overlap over a stretch whose middle is midway between the
		// two centres: each is shoved 5 m off its own centre, one ahead of it and one astern, and with
		// equal and opposite pushes both swing the same way - the behaviour that was simply missing before.
		// Equal up to the rounding of fixed-point products, which round towards minus infinity: the two
		// levers are mirror images, so the same scale factors can land an ulp apart.
		PushPair(MakeUnit(1, here, ship),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(8), fixed::FromInt(10)}, ship), pushes, spins);
		TS_ASSERT(!pushes[0].IsZero());
		TS_ASSERT_DIFFERS(spins[0], fixed::Zero());
		TS_ASSERT_DELTA(spins[0].ToDouble(), spins[1].ToDouble(), 0.0001);

		// Overlapping only where one's bow lies alongside the other's stern, each is shoved 14 m off its
		// own centre but at opposite ends, so both swing the overlapping end away and turn the same way.
		PushPair(MakeUnit(1, here, ship),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(4), fixed::FromInt(28)}, ship), pushes, spins);
		TS_ASSERT_DIFFERS(spins[0], fixed::Zero());
		TS_ASSERT_DELTA(spins[0].ToDouble(), spins[1].ToDouble(), 0.0001);
	}

	void test_CarryPushSpin()
	{
		// Negligible spins snap to exactly zero, so a decaying spin can't creep forever.
		TS_ASSERT_EQUALS(MomentumModel::CarryPushSpin(fixed::FromFraction(1, 1000), fixed::FromFraction(1, 1000)),
			fixed::Zero());
		// Otherwise it is simply what was carried plus what was applied.
		TS_ASSERT_EQUALS(MomentumModel::CarryPushSpin(fixed::FromInt(1), fixed::FromFraction(1, 2)),
			fixed::FromFraction(3, 2));

		// A unit whose pushes are one-shot (Drifting 0) can be pushed every turn without winding up: what
		// it carries in is always zero, so the total each turn is one turn's worth.
		const fixed perTurn{fixed::FromFraction(1, 2)};
		fixed carriedIn{fixed::Zero()};
		for (int turn = 0; turn < 10; ++turn)
		{
			TS_ASSERT_EQUALS(carriedIn + perTurn, perTurn);
			carriedIn = MomentumModel::CarryPushSpin(carriedIn, perTurn).Multiply(fixed::Zero());
		}

		// With Drifting 0.9 it does build up, but stays below the geometric limit of 1 / (1 - 0.9) turns.
		carriedIn = fixed::Zero();
		for (int turn = 0; turn < 60; ++turn)
			carriedIn = MomentumModel::CarryPushSpin(carriedIn, perTurn).Multiply(fixed::FromFraction(9, 10));
		TS_ASSERT_LESS_THAN(perTurn, carriedIn);
		TS_ASSERT_LESS_THAN(carriedIn, perTurn * 10);
	}

	// The push a pair exchanges, as the first unit sees it.
	static fixed PushMagnitude(const UnitCollision::MotionStateCompact& a, const UnitCollision::MotionStateCompact& b)
	{
		std::vector<CFixedVector2D> pushes;
		std::vector<fixed> spins;
		PushPair(a, b, pushes, spins);
		return pushes.size() == 2 ? pushes[0].Length() : fixed::Zero();
	}

	// The furthest apart (along X, ships along Z) two identical moving units can be while their push
	// still clears MinimalForce - the gate the motion manager drops pushes at - found by bisection.
	static double SignificantPushReach(const UnitCollision::CollisionShape& body)
	{
		const fixed threshold{PushConfig::GetInstance().GetRuntimeConfig().minimalPushing};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		double low = 0.0, high = 100.0;
		for (int i = 0; i < 24; ++i)
		{
			const double mid{(low + high) / 2};
			const CFixedVector2D other{fixed::FromInt(100) + fixed::FromDouble(mid), fixed::FromInt(100)};
			if (PushMagnitude(MakeUnit(1, here, body), MakeUnit(2, other, body)) > threshold)
				low = mid;
			else
				high = mid;
		}
		return low;
	}

	void test_Pushing_fires_before_solid_bodies_overlap()
	{
		// Two warships (radius 5) must be pushing hard enough to count by the time their hulls meet, at 10 m
		// between axes, not only once they have sailed into each other.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		TS_ASSERT_LESS_THAN_EQUALS((ship.radius * 2).ToDouble(), SignificantPushReach(ship));

		// Infantry already fired outside their bodies, at the spread's own distance; the floor that fixes
		// ships must leave that exactly where it was.
		const UnitCollision::CollisionShape infantry{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const PushConfig::RuntimeConfig& cfg{PushConfig::GetInstance().GetRuntimeConfig()};
		const fixed combinedClearance{(infantry.radius * 2).Multiply(PushConfig::PUSHING_CORRECTION)};
		const fixed maxDist{combinedClearance.Multiply(cfg.radiusMultiplier) + cfg.movingPushExtension};
		TS_ASSERT_DELTA(SignificantPushReach(infantry), maxDist.Multiply(cfg.movingPushingSpread).ToDouble(), 0.01);
	}

	void test_Pushing_idle_units_and_long_solid_bodies()
	{
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const UnitCollision::CollisionShape infantry{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		std::vector<CFixedVector2D> pushes;
		std::vector<fixed> spins;

		// A sailing ship and an anchored one push each other apart, both of them.
		PushPair(MakeUnit(1, here, ship), MakeUnit(2, here + CFixedVector2D{fixed::FromInt(8), fixed::Zero()}, ship,
			entity_angle_t::Zero(), false), pushes, spins);
		TS_ASSERT(!pushes[0].IsZero());
		TS_ASSERT(!pushes[1].IsZero());

		// A walking infantryman and an idle one leave each other alone, as they always have.
		PushPair(MakeUnit(1, here, infantry), MakeUnit(2, here + CFixedVector2D{fixed::FromInt(1), fixed::Zero()},
			infantry, entity_angle_t::Zero(), false), pushes, spins);
		TS_ASSERT(pushes[0].IsZero());
		TS_ASSERT(pushes[1].IsZero());

		// But a walking infantryman does deflect off an anchored ship.
		PushPair(MakeUnit(1, here, infantry), MakeUnit(2, here + CFixedVector2D{fixed::FromInt(3), fixed::Zero()},
			ship, entity_angle_t::Zero(), false), pushes, spins);
		TS_ASSERT(!pushes[0].IsZero());
		TS_ASSERT(!pushes[1].IsZero());

		// Cavalry are longer than they are wide but collide as circles, like everyone but the ships: a rider passing an idle
		// footman, or a footman passing an idle horse, leave each other alone exactly as two footmen do. A
		// herd or a parked squadron would otherwise be shoved about by everyone walking past.
		const UnitCollision::CollisionShape cavalry{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		TS_ASSERT(cavalry.IsCircle());
		TS_ASSERT(!ship.IsCircle());
		PushPair(MakeUnit(1, here, cavalry), MakeUnit(2, here + CFixedVector2D{fixed::FromInt(1), fixed::Zero()},
			infantry, entity_angle_t::Zero(), false), pushes, spins);
		TS_ASSERT(pushes[0].IsZero());
		TS_ASSERT(pushes[1].IsZero());
	}

	void test_ContactBetween_is_between_the_bodies()
	{
		CFixedVector2D contactA, contactB;

		// Two round units, the second a little off the first's line and ahead of it: the contact is their
		// centres, so the blow runs mostly forward and the first is turned about nothing at all. This is
		// the cavalry-into-infantry case, and taking the contact from a wide charge zone instead would
		// shove the target straight sideways and hand the charger a lever it doesn't have.
		const UnitCollision::CollisionShape round{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const CFixedVector2D charger{fixed::FromInt(50), fixed::FromInt(50)};
		const CFixedVector2D target{fixed::FromInt(51), fixed::FromInt(53)};
		UnitCollision::ContactBetween(round, charger, entity_angle_t::Zero(), round, target, entity_angle_t::Zero(),
			contactA, contactB);
		TS_ASSERT_EQUALS(contactA, charger);
		TS_ASSERT_EQUALS(contactB, target);
		CFixedVector2D along{contactB - contactA};
		along.Normalize();
		TS_ASSERT_LESS_THAN(fixed::FromFraction(9, 10), along.Dot(CFixedVector2D{fixed::Zero(), fixed::FromInt(1)}));

		// A ram (body along Z) meeting a ship lying broadside (body along X) ahead of it: each contact is
		// on its own body, so neither lever can exceed the body that carries it.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const CFixedVector2D ram{fixed::FromInt(50), fixed::FromInt(50)};
		const CFixedVector2D broadside{fixed::FromInt(55), fixed::FromInt(80)};
		UnitCollision::ContactBetween(ship, ram, entity_angle_t::Zero(), ship, broadside, fixed::Pi() / 2,
			contactA, contactB);
		TS_ASSERT_LESS_THAN_EQUALS((contactA - ram).Length(), ship.Extent() + fixed::Epsilon() * 4);
		TS_ASSERT_LESS_THAN_EQUALS((contactB - broadside).Length(), ship.Extent() + fixed::Epsilon() * 4);
		// The ram meets it with its bow, the broadside ship with its side.
		TS_ASSERT_DELTA((contactA - ram).Length().ToDouble(), ship.halfLength.ToDouble(), 0.05);
	}

	void test_Pushing_scales_with_long_solid_bodies_only()
	{
		// At full overlap every pair gets the same distance factor, so the push is set by the per-turn
		// amount alone: a quarter of a metre for two equal weights at the reference turn length.
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		const UnitCollision::CollisionShape infantry{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const fixed infantryPush{PushMagnitude(MakeUnit(1, here, infantry),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(1), fixed::Zero()}, infantry))};
		TS_ASSERT_DELTA(infantryPush.ToDouble(), 0.25, 0.001);

		// Two warships, radius 6, are (6 + 6) / (0.8 + 0.8) = 7.5 times the size of an infantry pair, and are
		// pushed by the square root of that, since a ship carries its pushes on as momentum, times whatever
		// SolidPushScale pathfinder.xml sets for a pair of solid units.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const fixed shipPush{PushMagnitude(MakeUnit(1, here, ship),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(8), fixed::Zero()}, ship))};
		const double solidScale{PushConfig::GetInstance().GetRuntimeConfig().solidPushScale.ToDouble()};
		TS_ASSERT_DELTA(shipPush.ToDouble(), 0.25 * std::sqrt(7.5) * solidScale, 0.01);

		// An elephant is bigger than a footman as well, but it collides as the circle of its 3 m clearance,
		// and its pushing was tuned as it is: exactly the infantry amount.
		const UnitCollision::CollisionShape elephant{entity_pos_t::FromInt(3), entity_pos_t::Zero(), false, false};
		TS_ASSERT(elephant.IsCircle());
		const fixed elephantPush{PushMagnitude(MakeUnit(1, here, elephant),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(3), fixed::Zero()}, elephant))};
		TS_ASSERT_EQUALS(elephantPush, infantryPush);
	}

	void test_MovementFilter_can_pass_unit_shapes()
	{
		// Unit shapes carry even tags, static ones odd.
		const ICmpObstructionManager::tag_t unitTag{2};
		const ICmpObstructionManager::tag_t staticTag{3};
		TS_ASSERT(unitTag.IsUnit());
		TS_ASSERT(!staticTag.IsUnit());

		const ICmpObstructionManager::flags_t blocking{ICmpObstructionManager::FLAG_BLOCK_MOVEMENT};
		const ICmpObstructionManager::flags_t blockingAndMoving{
			static_cast<ICmpObstructionManager::flags_t>(blocking | ICmpObstructionManager::FLAG_MOVING)};
		// The mover is group 1; the shapes belong to group 2, so the control-group skip never applies.
		const entity_id_t mover{1}, other{2};

		// As always with pushing on: an idle unit blocks, a moving one doesn't, a building does.
		const ControlGroupMovementObstructionFilter usual{false, mover};
		TS_ASSERT(usual.TestShape(unitTag, blocking, other, INVALID_ENTITY));
		TS_ASSERT(!usual.TestShape(unitTag, blockingAndMoving, other, INVALID_ENTITY));
		TS_ASSERT(usual.TestShape(staticTag, blocking, other, INVALID_ENTITY));

		// A solid unit: no unit shape blocks it, idle or moving, while buildings still do.
		const ControlGroupMovementObstructionFilter solidFilter{false, mover, false};
		TS_ASSERT(!solidFilter.TestShape(unitTag, blocking, other, INVALID_ENTITY));
		TS_ASSERT(!solidFilter.TestShape(unitTag, blockingAndMoving, other, INVALID_ENTITY));
		TS_ASSERT(solidFilter.TestShape(staticTag, blocking, other, INVALID_ENTITY));

		// The variant that also skips one specific tag behaves the same way.
		const SkipTagAndControlGroupObstructionFilter solidIgnoringOne{ICmpObstructionManager::tag_t{4}, false, mover, false};
		TS_ASSERT(!solidIgnoringOne.TestShape(unitTag, blocking, other, INVALID_ENTITY));
		TS_ASSERT(solidIgnoringOne.TestShape(staticTag, blocking, other, INVALID_ENTITY));
		const SkipTagAndControlGroupObstructionFilter usualIgnoringOne{ICmpObstructionManager::tag_t{4}, false, mover};
		TS_ASSERT(usualIgnoringOne.TestShape(unitTag, blocking, other, INVALID_ENTITY));
	}

	void test_Pushing_contact_impulse_stops_closing_solid_bodies()
	{
		// Two warships (radius 6) abreast 10 m apart: their hulls overlap, and they are sailing into each
		// other at 4 m/s each. offset runs from the second to the first, so closing is 8 m/s and equal
		// weights each lose half of it: both are brought to a stop across the contact, at once.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		const CFixedVector2D beside{fixed::FromInt(110), fixed::FromInt(100)};
		const CFixedVector2D east{fixed::FromInt(4), fixed::Zero()};
		const CFixedVector2D west{fixed::FromInt(-4), fixed::Zero()};
		std::vector<CFixedVector2D> pushes, velocityChanges;
		std::vector<fixed> spins;

		PushPair(MakeUnit(1, here, ship, entity_angle_t::Zero(), true, east),
			MakeUnit(2, beside, ship, entity_angle_t::Zero(), true, west), pushes, spins, velocityChanges);
		TS_ASSERT_DELTA(velocityChanges[0].X.ToDouble(), -4.0, 0.01);
		TS_ASSERT_DELTA(velocityChanges[1].X.ToDouble(), 4.0, 0.01);
		TS_ASSERT_EQUALS(velocityChanges[0].Y, fixed::Zero());
		// The soft push is still there to ease the overlap apart afterwards.
		TS_ASSERT(!pushes[0].IsZero());

		// Sailing apart instead: the impulse acts on the closing component only, so nothing.
		PushPair(MakeUnit(1, here, ship, entity_angle_t::Zero(), true, west),
			MakeUnit(2, beside, ship, entity_angle_t::Zero(), true, east), pushes, spins, velocityChanges);
		TS_ASSERT(velocityChanges[0].IsZero());
		TS_ASSERT(velocityChanges[1].IsZero());

		// Closing but not yet touching (13 m between axes, radii 6 + 6): nothing either - it is the soft
		// push's job to keep them apart before contact, not a hit's.
		PushPair(MakeUnit(1, here, ship, entity_angle_t::Zero(), true, east),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(13), fixed::Zero()}, ship, entity_angle_t::Zero(), true, west),
			pushes, spins, velocityChanges);
		TS_ASSERT(velocityChanges[0].IsZero());

		// A light boat (weight 10) running into an anchored warship (weight 200): the boat loses nearly all
		// of its 4 m/s and the warship is nudged by the rest, in proportion - momentum is conserved.
		UnitCollision::MotionStateCompact boat{MakeUnit(1, here, ship, entity_angle_t::Zero(), true, east)};
		UnitCollision::MotionStateCompact warship{MakeUnit(2, beside, ship, entity_angle_t::Zero(), false)};
		warship.weight = fixed::FromInt(200);
		PushPair(boat, warship, pushes, spins, velocityChanges);
		TS_ASSERT_DELTA(velocityChanges[0].X.ToDouble(), -4.0 * 200 / 210, 0.02);
		TS_ASSERT_DELTA(velocityChanges[1].X.ToDouble(), 4.0 * 10 / 210, 0.02);

		// Two elephants (4.5 x 9) walking shoulder to shoulder, touching, drifting together at half a metre
		// a second each: a brush, which the soft push turns around by itself, so no impulse and no spin.
		// Running into each other at 4 m/s each, the push can't keep up, and the impulse stops them.
		const UnitCollision::CollisionShape elephant{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromFraction(9, 2), entity_pos_t::FromInt(9))};
		const CFixedVector2D nearby{fixed::FromInt(104), fixed::FromInt(100)};
		const CFixedVector2D drift{fixed::FromFraction(1, 2), fixed::Zero()};
		PushPair(MakeUnit(1, here, elephant, entity_angle_t::Zero(), true, drift),
			MakeUnit(2, nearby, elephant, entity_angle_t::Zero(), true, -drift), pushes, spins, velocityChanges);
		TS_ASSERT(velocityChanges[0].IsZero());
		TS_ASSERT(velocityChanges[1].IsZero());
		TS_ASSERT(!pushes[0].IsZero());
		PushPair(MakeUnit(1, here, elephant, entity_angle_t::Zero(), true, east),
			MakeUnit(2, nearby, elephant, entity_angle_t::Zero(), true, west), pushes, spins, velocityChanges);
		TS_ASSERT_DELTA(velocityChanges[0].X.ToDouble(), -4.0, 0.01);

		// Two round units closing on each other get no contact impulse: crowds squash and spring back.
		const UnitCollision::CollisionShape infantry{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		PushPair(MakeUnit(1, here, infantry, entity_angle_t::Zero(), true, east),
			MakeUnit(2, here + CFixedVector2D{fixed::FromInt(1), fixed::Zero()}, infantry, entity_angle_t::Zero(), true, west),
			pushes, spins, velocityChanges);
		TS_ASSERT(velocityChanges[0].IsZero());
	}

	// The soft push each unit of @p units ends up with from one pass.
	static std::vector<CFixedVector2D> PushesOf(const std::vector<UnitCollision::MotionStateCompact>& units,
		const Grid<NavcellData>* passability = nullptr)
	{
		std::vector<CFixedVector2D> pushes, velocityChanges, corrections;
		std::vector<fixed> spins, hitSpins;
		std::vector<UnitCollision::ContactHit> hits;
		std::vector<uint8_t> pressures;
		UnitCollision::SpatialGrids grids;
		UnitCollision::ComputePushes(units, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
			velocityChanges, hitSpins, hits, pressures, corrections, passability);
		return pushes;
	}

	// The contact impulses every unit of @p units takes in one pass.
	static std::vector<CFixedVector2D> PushAll(const std::vector<UnitCollision::MotionStateCompact>& units)
	{
		std::vector<CFixedVector2D> pushes;
		std::vector<fixed> spins;
		std::vector<UnitCollision::ContactHit> hits;
		std::vector<CFixedVector2D> velocityChanges;
		UnitCollision::SpatialGrids grids;
		std::vector<fixed> hitSpins;
		std::vector<uint8_t> pressures;

		std::vector<CFixedVector2D> corrections;
		UnitCollision::ComputePushes(units, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
			velocityChanges, hitSpins, hits, pressures, corrections);
		return velocityChanges;
	}

	void test_PointVelocity()
	{
		const CFixedVector2D bow{fixed::Zero(), fixed::FromInt(20)};
		// Turning with a positive angle swings the bow, out along +Z, towards +X: the same way a blow on it towards
		// +X would spin it (see test_SpinFromVelocityChange).
		const CFixedVector2D swing{UnitCollision::PointVelocity(CFixedVector2D{}, fixed::FromFraction(1, 2), bow)};
		TS_ASSERT_DELTA(swing.X.ToDouble(), 10.0, 0.01);
		TS_ASSERT_DELTA(swing.Y.ToDouble(), 0.0, 0.01);
		TS_ASSERT_LESS_THAN(fixed::Zero(), UnitCollision::SpinFromVelocityChange(bow, swing, fixed::FromInt(100)));
		// The centre, and a body not turning, move with the body only.
		const CFixedVector2D east{fixed::FromInt(3), fixed::Zero()};
		TS_ASSERT_EQUALS(UnitCollision::PointVelocity(east, fixed::FromFraction(1, 2), CFixedVector2D{}), east);
		TS_ASSERT_EQUALS(UnitCollision::PointVelocity(east, fixed::Zero(), bow), east);
	}

	void test_Pushing_contact_impulse_sees_a_turning_hull()
	{
		// Two warships (12 x 48) abreast, 11 m apart across, the second one further along: the first one's bow touches
		// the second one's stern. Neither centre moves.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const auto impulseOn = [&](const fixed turning) {
			UnitCollision::MotionStateCompact first{MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, ship)};
			first.angularVelocity = turning;
			const UnitCollision::MotionStateCompact second{
				MakeUnit(2, CFixedVector2D{fixed::FromInt(111), fixed::FromInt(136)}, ship)};
			std::vector<CFixedVector2D> pushes, velocityChanges;
			std::vector<fixed> spins;
			PushPair(first, second, pushes, spins, velocityChanges);
			return velocityChanges[0];
		};
		// Swinging its bow into the other ship at half a radian a second, 9 m/s at the bow: stopped.
		TS_ASSERT_LESS_THAN(impulseOn(fixed::FromFraction(1, 2)).X, fixed::Zero());
		// Swinging it away, or not turning: nothing to stop.
		TS_ASSERT(impulseOn(-fixed::FromFraction(1, 2)).IsZero());
		TS_ASSERT(impulseOn(fixed::Zero()).IsZero());
	}

	// The overlap corrections every unit of @p units gets in one pass.
	static std::vector<CFixedVector2D> CorrectionsOf(const std::vector<UnitCollision::MotionStateCompact>& units)
	{
		UnitCollision::SpatialGrids grids;
		std::vector<CFixedVector2D> pushes, velocityChanges, corrections;
		std::vector<fixed> spins, hitSpins;
		std::vector<UnitCollision::ContactHit> hits;
		std::vector<uint8_t> pressures;
		UnitCollision::ComputePushes(units, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
			velocityChanges, hitSpins, hits, pressures, corrections);
		return corrections;
	}

	void test_Pushing_overlapping_solid_bodies_are_moved_apart_by_depth()
	{
		// Two elephants (4.5 x 9, radius 2.25) side by side and at rest, their axes 3.5 m apart: 1 m deep in each
		// other. Nothing is closing, so only the correction can get them out faster than the soft push.
		const UnitCollision::CollisionShape elephant{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromFraction(9, 2), entity_pos_t::FromInt(9))};
		const fixed share{PushConfig::GetInstance().GetRuntimeConfig().solidOverlapCorrection};
		const auto pairAt = [&](const fixed apart) {
			return CorrectionsOf({
				MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, elephant, entity_angle_t::Zero(), false),
				MakeUnit(2, CFixedVector2D{fixed::FromInt(100) + apart, fixed::FromInt(100)}, elephant, entity_angle_t::Zero(), false)});
		};
		const std::vector<CFixedVector2D> shallow{pairAt(fixed::FromFraction(7, 2))};
		// Equal weights: each is moved half of the share of the depth, away from the other.
		TS_ASSERT_DELTA(shallow[0].X.ToDouble(), -share.ToDouble() / 2, 0.01);
		TS_ASSERT_DELTA(shallow[1].X.ToDouble(), share.ToDouble() / 2, 0.01);
		// Twice as deep, twice the correction.
		const std::vector<CFixedVector2D> deep{pairAt(fixed::FromFraction(5, 2))};
		TS_ASSERT_DELTA(deep[1].X.ToDouble(), share.ToDouble(), 0.01);
		// Not overlapping: nothing.
		TS_ASSERT(pairAt(fixed::FromInt(5)).at(0).IsZero());

		// A ship twenty times heavier gives way twenty times less.
		UnitCollision::MotionStateCompact heavy{MakeUnit(2, CFixedVector2D{fixed::FromFraction(207, 2), fixed::FromInt(100)},
			elephant, entity_angle_t::Zero(), false)};
		heavy.weight = fixed::FromInt(200);
		const std::vector<CFixedVector2D> uneven{CorrectionsOf({
			MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, elephant, entity_angle_t::Zero(), false), heavy})};
		TS_ASSERT_DELTA((uneven[0].Length() / uneven[1].Length()).ToDouble(), 20.0, 0.5);

		// Round bodies are left to the soft push, as crowds are meant to squash.
		const UnitCollision::CollisionShape round{entity_pos_t::FromInt(2), entity_pos_t::Zero(), false, false};
		TS_ASSERT(CorrectionsOf({
			MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, round, entity_angle_t::Zero(), false),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(103), fixed::FromInt(100)}, round, entity_angle_t::Zero(), false)}).at(0).IsZero());

		// Members of the same formation are neither moved apart nor stopped by each other, even when one
		// walks into the other.
		UnitCollision::MotionStateCompact member{MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, elephant,
			entity_angle_t::Zero(), true, CFixedVector2D{fixed::FromInt(4), fixed::Zero()})};
		UnitCollision::MotionStateCompact mate{MakeUnit(2, CFixedVector2D{fixed::FromFraction(207, 2), fixed::FromInt(100)}, elephant)};
		member.controlGroup = mate.controlGroup = 50;
		TS_ASSERT(CorrectionsOf({member, mate}).at(0).IsZero());
		TS_ASSERT(PushAll({member, mate}).at(0).IsZero());
		// Nor pushed: a solid member keeps to its slot, whoever stands in it.
		TS_ASSERT(PushesOf({member, mate}).at(0).IsZero());
	}

	void test_Pushing_contact_impulses_dont_stack()
	{
		// A ship (12 x 48) at rest, with two others alongside to the east, both sailing into it at 4 m/s. Each
		// pair alone would send it west at 2 m/s; together they must not send it at 4.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const CFixedVector2D west{fixed::FromInt(-4), fixed::Zero()};
		const std::vector<CFixedVector2D> velocityChanges{PushAll({
			MakeUnit(1, CFixedVector2D{fixed::FromInt(100), fixed::FromInt(100)}, ship, entity_angle_t::Zero(), false),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(110), fixed::FromInt(90)}, ship, entity_angle_t::Zero(), true, west),
			MakeUnit(3, CFixedVector2D{fixed::FromInt(110), fixed::FromInt(110)}, ship, entity_angle_t::Zero(), true, west)})};
		TS_ASSERT_LESS_THAN(velocityChanges[0].X, fixed::Zero());
		TS_ASSERT_DELTA(velocityChanges[0].Length().ToDouble(), 2.0, 0.05);
		// Each of the other two still gets its own.
		TS_ASSERT_DELTA(velocityChanges[1].Length().ToDouble(), 2.0, 0.05);
	}

	void test_Pushing_solid_pushes_dont_stack()
	{
		// A ship (12 x 48) at rest with one other alongside to the east is pushed west by some amount. With two
		// alongside to the east it is pushed no harder: a solid unit's pushes are capped at its strongest pusher.
		const UnitCollision::CollisionShape ship{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromInt(12), entity_pos_t::FromInt(48))};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		const std::vector<CFixedVector2D> one{PushesOf({
			MakeUnit(1, here, ship, entity_angle_t::Zero(), false),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(110), fixed::FromInt(90)}, ship, entity_angle_t::Zero(), false)})};
		const std::vector<CFixedVector2D> two{PushesOf({
			MakeUnit(1, here, ship, entity_angle_t::Zero(), false),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(110), fixed::FromInt(90)}, ship, entity_angle_t::Zero(), false),
			MakeUnit(3, CFixedVector2D{fixed::FromInt(110), fixed::FromInt(110)}, ship, entity_angle_t::Zero(), false)})};
		TS_ASSERT_LESS_THAN(one[0].X, fixed::Zero());
		TS_ASSERT_LESS_THAN(two[0].X, fixed::Zero());
		TS_ASSERT_DELTA(two[0].Length().ToDouble(), one[0].Length().ToDouble(), 0.01);

		// The pushes are summed before the cap, so one on each side still cancels out.
		const std::vector<CFixedVector2D> squeezed{PushesOf({
			MakeUnit(1, here, ship, entity_angle_t::Zero(), false),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(110), fixed::FromInt(100)}, ship, entity_angle_t::Zero(), false),
			MakeUnit(3, CFixedVector2D{fixed::FromInt(90), fixed::FromInt(100)}, ship, entity_angle_t::Zero(), false)})};
		// To the rounding of each push's room scaling.
		TS_ASSERT(squeezed[0].CompareLength(fixed::FromFraction(1, 1000)) < 0);
	}

	using Contacts = std::vector<MomentumModel::Contact>;

	void test_SlideAlongContacts()
	{
		const fixed dt{fixed::FromFraction(1, 5)};
		const CFixedVector2D east{fixed::FromInt(1), fixed::Zero()};
		const CFixedVector2D north{fixed::Zero(), fixed::FromInt(1)};
		const CFixedVector2D velocity{fixed::FromInt(3), fixed::FromInt(4)};
		const auto contact = [](const CFixedVector2D& normal, const int gapTenths) {
			return MomentumModel::Contact{normal, entity_pos_t::FromFraction(gapTenths, 10)};
		};

		// Nothing near: unchanged.
		TS_ASSERT_EQUALS(MomentumModel::SlideAlongContacts(velocity, Contacts{}, dt), velocity);
		// A body touching to the east (its normal points west): the eastward part is dropped, the rest kept.
		TS_ASSERT_EQUALS(MomentumModel::SlideAlongContacts(velocity, Contacts{contact(-east, 0)}, dt),
			CFixedVector2D(fixed::Zero(), fixed::FromInt(4)));
		// A gap of 0.4 m to it: the unit may close half of that this turn, 1 m/s, and no more.
		TS_ASSERT_EQUALS(MomentumModel::SlideAlongContacts(velocity, Contacts{contact(-east, 4)}, dt),
			CFixedVector2D(fixed::FromInt(1), fixed::FromInt(4)));
		// A gap of 2 m allows 5 m/s, more than it is doing: unchanged.
		TS_ASSERT_EQUALS(MomentumModel::SlideAlongContacts(velocity, Contacts{contact(-east, 20)}, dt), velocity);
		// A body to the west: nothing runs into it.
		TS_ASSERT_EQUALS(MomentumModel::SlideAlongContacts(velocity, Contacts{contact(east, 0)}, dt), velocity);
		// Touching one to the east and one to the north: both parts go.
		TS_ASSERT(MomentumModel::SlideAlongContacts(velocity, Contacts{contact(-east, 0), contact(-north, 0)}, dt).IsZero());

		// A touching body to the east that walks east at 2 m/s itself: the unit may follow it at that pace,
		// and one walking away as fast as the unit steers doesn't hold it at all.
		const auto following = [&](const int receding) {
			return MomentumModel::SlideAlongContacts(velocity,
				Contacts{MomentumModel::Contact{-east, entity_pos_t::Zero(), fixed::FromInt(receding)}}, dt);
		};
		TS_ASSERT_EQUALS(following(2).X, fixed::FromInt(2));
		TS_ASSERT_EQUALS(following(2).Y, velocity.Y);
		TS_ASSERT_EQUALS(following(3), velocity);
		TS_ASSERT(!MomentumModel::HeldByContact(velocity, Contacts{MomentumModel::Contact{-east, entity_pos_t::Zero(), fixed::FromInt(3)}}, dt));
	}

	void test_HeldByContact()
	{
		const fixed dt{fixed::FromFraction(1, 5)};
		const CFixedVector2D east{fixed::FromInt(1), fixed::Zero()};
		const CFixedVector2D north{fixed::Zero(), fixed::FromInt(1)};
		const CFixedVector2D heading{fixed::FromInt(3), fixed::FromInt(4)};
		const auto contact = [](const CFixedVector2D& normal, const int gapTenths) {
			return MomentumModel::Contact{normal, entity_pos_t::FromFraction(gapTenths, 10)};
		};
		// Steering east at 3 m/s into a touching body to the east (normal west): held. The same body 2 m off
		// allows 5 m/s this turn, so it isn't holding the unit yet; 1 m off allows 2.5 m/s, and is.
		TS_ASSERT(MomentumModel::HeldByContact(heading, Contacts{contact(-east, 0)}, dt));
		TS_ASSERT(!MomentumModel::HeldByContact(heading, Contacts{contact(-east, 20)}, dt));
		TS_ASSERT(MomentumModel::HeldByContact(heading, Contacts{contact(-east, 10)}, dt));
		// A body to the west (normal east), or one to the north (normal south) the unit moves away from: not held,
		// however close.
		TS_ASSERT(!MomentumModel::HeldByContact(heading, Contacts{contact(east, 0)}, dt));
		TS_ASSERT(!MomentumModel::HeldByContact(CFixedVector2D{fixed::FromInt(4), fixed::FromInt(-3)}, Contacts{contact(-north, 0)}, dt));
		TS_ASSERT(!MomentumModel::HeldByContact(heading, Contacts{}, dt));
		// Asked only about the bodies the unit gives way to, the same touching body holds it only when it is one.
		TS_ASSERT(!MomentumModel::HeldByContact(heading, Contacts{contact(-east, 0)}, dt, &MomentumModel::Contact::yield));
		TS_ASSERT(MomentumModel::HeldByContact(heading, Contacts{MomentumModel::Contact{-east, entity_pos_t::Zero(), fixed::Zero(), true}}, dt,
			&MomentumModel::Contact::yield));
	}

	void test_Pushing_reports_solid_contacts()
	{
		// Two elephants (4.5 x 9) abreast with their flanks touching are each reported in contact with a gap of
		// zero and the normal pointing away from the other. A metre apart they are still reported, with that
		// gap; out of push range not. A footman against an elephant is no contact: only solid bodies slide.
		const UnitCollision::CollisionShape elephant{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromFraction(9, 2), entity_pos_t::FromInt(9))};
		const UnitCollision::CollisionShape footman{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		const auto contactsOf = [](const std::vector<UnitCollision::MotionStateCompact>& units) {
			std::vector<CFixedVector2D> pushes, velocityChanges, corrections;
			std::vector<fixed> spins, hitSpins;
			std::vector<UnitCollision::ContactHit> hits;
			std::vector<uint8_t> pressures;
			std::vector<UnitCollision::Contact> contacts;
			UnitCollision::SpatialGrids grids;
			UnitCollision::ComputePushes(units, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
				velocityChanges, hitSpins, hits, pressures, corrections, nullptr, &contacts);
			return contacts;
		};

		const std::vector<UnitCollision::Contact> touching{contactsOf({
			MakeUnit(1, here, elephant),
			MakeUnit(2, CFixedVector2D{fixed::FromFraction(209, 2), fixed::FromInt(100)}, elephant)})};
		TS_ASSERT_EQUALS(touching.size(), static_cast<size_t>(2));
		TS_ASSERT_EQUALS(touching[0].unit, static_cast<size_t>(0));
		TS_ASSERT_LESS_THAN(touching[0].normal.X, fixed::Zero());
		TS_ASSERT_EQUALS(touching[0].gap, fixed::Zero());
		TS_ASSERT_EQUALS(touching[1].unit, static_cast<size_t>(1));
		TS_ASSERT_LESS_THAN(fixed::Zero(), touching[1].normal.X);

		const std::vector<UnitCollision::Contact> near{contactsOf({MakeUnit(1, here, elephant),
			MakeUnit(2, CFixedVector2D{fixed::FromFraction(211, 2), fixed::FromInt(100)}, elephant)})};
		TS_ASSERT_EQUALS(near.size(), static_cast<size_t>(2));
		TS_ASSERT_DELTA(near[0].gap.ToDouble(), 1.0, 0.01);
		TS_ASSERT(contactsOf({MakeUnit(1, here, elephant),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(140), fixed::FromInt(100)}, elephant)}).empty());

		TS_ASSERT(contactsOf({MakeUnit(1, here, elephant),
			MakeUnit(2, CFixedVector2D{fixed::FromInt(103), fixed::FromInt(100)}, footman)}).empty());

		// Members of the same formation are no contact to each other.
		UnitCollision::MotionStateCompact member{MakeUnit(1, here, elephant)};
		UnitCollision::MotionStateCompact mate{MakeUnit(2, CFixedVector2D{fixed::FromFraction(209, 2), fixed::FromInt(100)}, elephant)};
		member.controlGroup = mate.controlGroup = 50;
		TS_ASSERT(contactsOf({member, mate}).empty());
	}

	void test_Pushing_solid_push_needs_room()
	{
		// Two elephants (4.5 x 9) side by side on open ground push each other apart in full. With a packed group
		// of elephants on the ground beyond the eastern one, out of its push range but where its push would put
		// it, the push on it shrinks to the floor (DensityPushFloor): a push there would only be passed along the
		// group. The western one, with open ground behind it, is pushed as before.
		const UnitCollision::CollisionShape elephant{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromFraction(9, 2), entity_pos_t::FromInt(9))};
		// Half a metre apart: within the push range whatever pathfinder.xml says it is.
		const CFixedVector2D west{fixed::FromInt(101), fixed::FromInt(100)};
		const CFixedVector2D east{fixed::FromInt(106), fixed::FromInt(100)};
		const std::vector<CFixedVector2D> open{PushesOf({
			MakeUnit(1, west, elephant, entity_angle_t::Zero(), false),
			MakeUnit(2, east, elephant, entity_angle_t::Zero(), false)})};
		TS_ASSERT_LESS_THAN(open[0].X, fixed::Zero());
		TS_ASSERT_LESS_THAN(fixed::Zero(), open[1].X);
		const double floor{PushConfig::GetInstance().GetRuntimeConfig().densityPushFloor.ToDouble()};

		// The density grid starts a cell before the westernmost unit: a footman far out of everyone's way pins
		// that down, so the group below lands in the cells the eastern one's push looks at.
		const UnitCollision::CollisionShape footman{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		std::vector<UnitCollision::MotionStateCompact> units{
			MakeUnit(1, west, elephant, entity_angle_t::Zero(), false),
			MakeUnit(2, east, elephant, entity_angle_t::Zero(), false),
			MakeUnit(3, CFixedVector2D{fixed::FromInt(94), fixed::FromInt(80)}, footman, entity_angle_t::Zero(), false)};
		for (const int x : {119, 121})
			for (const int z : {94, 100, 106})
				units.push_back(MakeUnit(static_cast<entity_id_t>(units.size() + 1),
					CFixedVector2D{fixed::FromInt(x), fixed::FromInt(z)}, elephant, entity_angle_t::Zero(), false));
		const std::vector<CFixedVector2D> crowded{PushesOf(units)};
		TS_ASSERT_DELTA(crowded[1].X.ToDouble(), open[1].X.ToDouble() * floor, 0.001);
		TS_ASSERT_EQUALS(crowded[0], open[0]);

		// A wall right beyond the eastern one instead, impassable for the elephants' pass class, takes the ground
		// the same way; one the elephants may stand on (another class's) leaves the push in full.
		Grid<NavcellData> passability(140, 140);
		for (int i = 109; i < 140; ++i)
			for (int j = 0; j < 140; ++j)
				passability.set(i, j, 1);
		const std::vector<CFixedVector2D> walled{PushesOf({units[0], units[1], units[2]}, &passability)};
		TS_ASSERT_DELTA(walled[1].X.ToDouble(), open[1].X.ToDouble() * floor, 0.001);
		TS_ASSERT_EQUALS(walled[0], open[0]);
		for (int i = 109; i < 140; ++i)
			for (int j = 0; j < 140; ++j)
				passability.set(i, j, 2);
		const std::vector<CFixedVector2D> passable{PushesOf({units[0], units[1], units[2]}, &passability)};
		TS_ASSERT_EQUALS(passable[1], open[1]);
	}

	void test_Pushing_squeezed_pair_yields()
	{
		// Two elephants (4.5 x 9) on an order, abreast and half a metre apart. On open ground their pushes
		// part them and neither gives way. Between two walls, neither can be pushed aside: exactly one gives
		// way, and with as little room on both sides it is the higher entity. With the wall on one side
		// only, the push towards the open side has its room, and again neither does. Nor does a unit give way
		// to one without an order, nor when it is other elephants that take the room on both sides: that is
		// a crowd, not a gap.
		const UnitCollision::CollisionShape elephant{
			UnitCollision::DeriveCollisionShape(entity_pos_t::FromFraction(9, 2), entity_pos_t::FromInt(9))};
		const UnitCollision::CollisionShape footman{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const CFixedVector2D west{fixed::FromInt(101), fixed::FromInt(100)};
		const CFixedVector2D east{fixed::FromInt(106), fixed::FromInt(100)};
		// A footman far out of everyone's way pins down where the density grid starts.
		const CFixedVector2D corner{fixed::FromInt(94), fixed::FromInt(80)};
		const auto contactsOf = [&](const bool eastMoving, const Grid<NavcellData>* passability, const bool crowded = false) {
			std::vector<CFixedVector2D> pushes, velocityChanges, corrections;
			std::vector<fixed> spins, hitSpins;
			std::vector<UnitCollision::ContactHit> hits;
			std::vector<uint8_t> pressures;
			std::vector<UnitCollision::Contact> contacts;
			UnitCollision::SpatialGrids grids;
			std::vector<UnitCollision::MotionStateCompact> units{
				MakeUnit(1, west, elephant), MakeUnit(2, east, elephant, entity_angle_t::Zero(), eastMoving),
				MakeUnit(3, corner, footman, entity_angle_t::Zero(), false)};
			if (crowded)
				for (const int x : {85, 87, 115, 117})
					for (const int z : {94, 100, 106})
						units.push_back(MakeUnit(static_cast<entity_id_t>(units.size() + 1),
							CFixedVector2D{fixed::FromInt(x), fixed::FromInt(z)}, elephant, entity_angle_t::Zero(), false));
			UnitCollision::ComputePushes(units, PushConfig::IMPACT_REFERENCE_TIME, grids, pushes, spins,
				velocityChanges, hitSpins, hits, pressures, corrections, passability, &contacts);
			return contacts;
		};

		const std::vector<UnitCollision::Contact> open{contactsOf(true, nullptr)};
		TS_ASSERT_EQUALS(open.size(), static_cast<size_t>(2));
		TS_ASSERT(!open[0].yield && !open[1].yield);

		Grid<NavcellData> passability(140, 140);
		const auto wall = [&](const int from, const int to) {
			for (int i = from; i < to; ++i)
				for (int j = 0; j < 140; ++j)
					passability.set(i, j, 1);
		};
		wall(109, 140);
		const std::vector<UnitCollision::Contact> oneWall{contactsOf(true, &passability)};
		TS_ASSERT(!oneWall[0].yield && !oneWall[1].yield);

		wall(0, 98);
		const std::vector<UnitCollision::Contact> squeezed{contactsOf(true, &passability)};
		TS_ASSERT_EQUALS(squeezed.size(), static_cast<size_t>(2));
		TS_ASSERT(!squeezed[0].yield);
		TS_ASSERT(squeezed[1].yield);

		const std::vector<UnitCollision::Contact> idle{contactsOf(false, &passability)};
		TS_ASSERT(!idle.empty());
		TS_ASSERT(!idle[0].yield && !idle[1].yield);

		for (const UnitCollision::Contact& contact : contactsOf(true, nullptr, true))
			TS_ASSERT(!contact.yield);
	}

	void test_Charge_hits_what_it_runs_into()
	{
		// A cavalryman (weight 70, charging at 12 m/s) has ridden into a footman standing still. They are
		// round bodies and one of them is idle, so ordinary pushing would leave them alone - but a charge
		// is a collision, so the pair is taken: the footman is bowled over with 70/80 of the closing
		// speed, the rider loses 10/80 of it, and the contact is reported as a hit with those numbers.
		const UnitCollision::CollisionShape infantry{entity_pos_t::FromFraction(8, 10), entity_pos_t::Zero(), false, false};
		const CFixedVector2D here{fixed::FromInt(100), fixed::FromInt(100)};
		const CFixedVector2D ahead{fixed::FromInt(100), fixed::FromInt(101)};
		const CFixedVector2D north{fixed::Zero(), fixed::FromInt(12)};
		std::vector<CFixedVector2D> pushes, velocityChanges;
		std::vector<fixed> spins;
		std::vector<UnitCollision::ContactHit> hits;

		UnitCollision::MotionStateCompact rider{MakeUnit(1, here, infantry, entity_angle_t::Zero(), true, north)};
		rider.weight = fixed::FromInt(70);
		rider.charging = true;
		rider.minChargeSpeed = fixed::FromFraction(15, 2);
		const UnitCollision::MotionStateCompact footman{MakeUnit(2, ahead, infantry, entity_angle_t::Zero(), false)};

		PushPair(rider, footman, pushes, spins, velocityChanges, hits);
		TS_ASSERT_EQUALS(hits.size(), 1u);
		TS_ASSERT_EQUALS(hits[0].charger, 1u);
		TS_ASSERT_EQUALS(hits[0].target, 2u);
		TS_ASSERT_DELTA(hits[0].targetSpeedChange.ToDouble(), 12.0 * 70 / 80, 0.02);
		TS_ASSERT_DELTA(hits[0].chargerSpeedChange.ToDouble(), 12.0 * 10 / 80, 0.02);
		TS_ASSERT_DELTA(velocityChanges[1].Y.ToDouble(), 12.0 * 70 / 80, 0.02);
		TS_ASSERT_DELTA(velocityChanges[0].Y.ToDouble(), -12.0 * 10 / 80, 0.02);

		// Too slow to be a charge (it isn't flagged charging): back to ordinary pushing, where a moving
		// footman and an idle one leave each other alone. Nothing at all happens.
		UnitCollision::MotionStateCompact walker{MakeUnit(1, here, infantry, entity_angle_t::Zero(), true,
			CFixedVector2D{fixed::Zero(), fixed::FromInt(3)})};
		walker.weight = fixed::FromInt(70);
		PushPair(walker, footman, pushes, spins, velocityChanges, hits);
		TS_ASSERT(hits.empty());
		TS_ASSERT(pushes[0].IsZero());
		TS_ASSERT(velocityChanges[1].IsZero());

		// Charging past, not into: bodies 1.6 m apart don't touch (radii 0.8 + 0.8), so no hit and no
		// knockback, though the soft push still keeps them apart.
		const CFixedVector2D beside{fixed::FromInt(102), fixed::FromInt(100)};
		PushPair(rider, MakeUnit(2, beside, infantry, entity_angle_t::Zero(), false), pushes, spins, velocityChanges, hits);
		TS_ASSERT(hits.empty());
		TS_ASSERT(velocityChanges[1].IsZero());

		// Two chargers meeting head-on have each hit the other.
		UnitCollision::MotionStateCompact other{MakeUnit(2, ahead, infantry, entity_angle_t::Zero(), true, -north)};
		other.weight = fixed::FromInt(70);
		other.charging = true;
		other.minChargeSpeed = fixed::FromFraction(15, 2);
		PushPair(rider, other, pushes, spins, velocityChanges, hits);
		TS_ASSERT_EQUALS(hits.size(), 2u);
		TS_ASSERT_DELTA(hits[0].targetSpeedChange.ToDouble(), 12.0, 0.02);
	}

	void test_RollFromVelocityChange()
	{
		MotionState state(nullptr, nullptr, nullptr);
		state.angle = entity_angle_t::Zero();  // facing +Z, so +X is to the right
		const CFixedVector2D shovedRight{fixed::FromInt(4), fixed::Zero()};
		const ChargeConfig::Values& config{ChargeConfig::Get()};

		// A unit that can't roll never does.
		state.maxRollAngle = fixed::Zero();
		TS_ASSERT_EQUALS(ImpactEffects::RollFromVelocityChange(state, shovedRight), fixed::Zero());

		// One that can leans the way it was shoved: to the right is a positive roll, and only the sideways
		// part counts, so a shove straight ahead leans it not at all.
		state.maxRollAngle = fixed::FromInt(1);
		const fixed lean{ImpactEffects::RollFromVelocityChange(state, shovedRight)};
		TS_ASSERT_LESS_THAN(fixed::Zero(), lean);
		TS_ASSERT_DELTA(lean.ToDouble(), 4.0 * config.rollStrength.ToDouble(), 0.01);
		TS_ASSERT_EQUALS(ImpactEffects::RollFromVelocityChange(state, -shovedRight), -lean);
		TS_ASSERT_EQUALS(ImpactEffects::RollFromVelocityChange(state,
			CFixedVector2D{fixed::Zero(), fixed::FromInt(4)}), fixed::Zero());

		// Capped as AddRollImpulse caps, so a huge blow can't throw a ship over.
		const fixed cap{state.maxRollAngle.Multiply(config.rollMaxVelocityScale)};
		TS_ASSERT_EQUALS(ImpactEffects::RollFromVelocityChange(state,
			CFixedVector2D{fixed::FromInt(1000), fixed::Zero()}), cap);

		// A negligible lean is dropped rather than left to creep.
		TS_ASSERT_EQUALS(ImpactEffects::RollFromVelocityChange(state,
			CFixedVector2D{fixed::FromFraction(1, 1000), fixed::Zero()}), fixed::Zero());
	}

};
