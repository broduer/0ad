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
#include "simulation2/helpers/HeadingControl.h"
#include "simulation2/helpers/MomentumModel.h"
#include "simulation2/helpers/PathFollowing.h"
#include "simulation2/helpers/Pathfinding.h"

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

	void test_CarryExternalInertia()
	{
		const CFixedVector2D tiny{fixed::FromFraction(1, 1000), fixed::Zero()};
		TS_ASSERT_EQUALS(MomentumModel::CarryExternalInertia(tiny, tiny), CFixedVector2D{});

		const CFixedVector2D carried{fixed::FromInt(1), fixed::Zero()};
		const CFixedVector2D applied{fixed::Zero(), fixed::FromInt(1)};
		TS_ASSERT_EQUALS(MomentumModel::CarryExternalInertia(carried, applied), CFixedVector2D(fixed::FromInt(1), fixed::FromInt(1)));
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
};
