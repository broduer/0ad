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
#include "UnitCollision.h"

#include "simulation2/helpers/ChargeConfig.h"
#include "simulation2/helpers/Geometry.h"

#include <algorithm>
#include <cstdint>
#include <utility>

UnitCollision::CollisionShape UnitCollision::DeriveCollisionShape(
	const entity_pos_t footprintWidth, const entity_pos_t footprintDepth)
{
	const entity_pos_t shortSide{std::min(footprintWidth, footprintDepth)};
	const entity_pos_t longSide{std::max(footprintWidth, footprintDepth)};
	return CollisionShape{shortSide / 2, (longSide - shortSide) / 2, footprintWidth > footprintDepth, true};
}

entity_pos_t UnitCollision::CollisionShape::InertiaPerMass() const
{
	const entity_pos_t extent{Extent()};
	return (radius.Multiply(radius) + extent.Multiply(extent)) / 3;
}

fixed UnitCollision::SpinFromVelocityChange(const CFixedVector2D& lever, const CFixedVector2D& velocityChange,
	const entity_pos_t inertiaPerMass)
{
	// A circular body is always struck through its centre, so this is the common case by far.
	if (lever.IsZero() || inertiaPerMass <= entity_pos_t::Zero())
		return fixed::Zero();

	// cross(velocityChange, lever), which is -cross(lever, velocityChange): a positive entity angle turns
	// +Z towards +X (see CFixedVector2D::Rotate), the opposite way round from the usual cross product,
	// so the sign is flipped here to give a spin that IntegrateSpin can add to the angle as it is.
	// Both are short relative vectors - the lever is bounded by the body's extent, a few tens of metres
	// at most, and a velocity change by a few metres per second - so their products stay well inside
	// what Dot can hold.
	const fixed torque{velocityChange.Dot(lever.Perpendicular())};
	return torque / inertiaPerMass;
}

fixed UnitCollision::ContactBetween(
	const CollisionShape& a, const CFixedVector2D& positionA, const entity_angle_t angleA,
	const CollisionShape& b, const CFixedVector2D& positionB, const entity_angle_t angleB,
	CFixedVector2D& outContactA, CFixedVector2D& outContactB)
{
	CFixedVector2D aStart, aEnd, bStart, bEnd;
	a.GetSegment(positionA, angleA, aStart, aEnd);
	b.GetSegment(positionB, angleB, bStart, bEnd);
	return Geometry::DistanceSegmentToSegment(aStart, aEnd, bStart, bEnd, outContactA, outContactB);
}

void UnitCollision::CollisionShape::GetSegment(const CFixedVector2D& position, const entity_angle_t angle,
	CFixedVector2D& out0, CFixedVector2D& out1) const
{
	if (IsCircle())
	{
		out0 = position;
		out1 = position;
		return;
	}

	fixed s, c;
	sincos_approx(angle, s, c);
	// Matching CCmpObstructionManager::GetStaticShapeObstruction: an entity's width runs along
	// (c, -s) and its depth along (s, c).
	const CFixedVector2D axis{alongWidth ? CFixedVector2D{c, -s} : CFixedVector2D{s, c}};
	const CFixedVector2D half{axis.Multiply(halfLength)};
	out0 = position - half;
	out1 = position + half;
}

void UnitCollision::BuildSpatialGrid(const std::vector<MotionStateCompact>& units, int gridSize,
									   Grid<std::vector<size_t>>& spatialGrid, std::vector<std::pair<int, int>>& filledCells,
									   int& gridWidth, int& gridHeight)
{
	// Only the cells we filled last turn can hold anything, so clearing those is proportional to the units,
	// not to the map: with cells sized to the push range there are far more cells than units.
	for (const std::pair<int, int>& cell : filledCells)
		spatialGrid.get(cell.first, cell.second).clear();
	filledCells.clear();

	if (units.empty())
		return;

	entity_pos_t minX = units[0].position.X, maxX = units[0].position.X;
	entity_pos_t minZ = units[0].position.Y, maxZ = units[0].position.Y;

	for (const auto& unit : units)
	{
		if (unit.position.X < minX) minX = unit.position.X;
		if (unit.position.X > maxX) maxX = unit.position.X;
		if (unit.position.Y < minZ) minZ = unit.position.Y;
		if (unit.position.Y > maxZ) maxZ = unit.position.Y;
	}

	// One cell of slack, so a unit exactly on the edge still has its neighbouring cells inside the grid.
	const entity_pos_t margin{entity_pos_t::FromInt(gridSize)};
	minX -= margin;
	maxX += margin;
	minZ -= margin;
	maxZ += margin;

	entity_pos_t width = maxX - minX;
	entity_pos_t height = maxZ - minZ;

	gridWidth = (width / gridSize).ToInt_RoundToNearest() + 1;
	gridHeight = (height / gridSize).ToInt_RoundToNearest() + 1;

	if (gridWidth < 1) gridWidth = 1;
	if (gridHeight < 1) gridHeight = 1;

	// Resizing reallocates every cell, so only do it when the grid actually changes shape.
	if (spatialGrid.width() != gridWidth || spatialGrid.height() != gridHeight)
		spatialGrid.resize(static_cast<std::uint16_t>(gridWidth), static_cast<std::uint16_t>(gridHeight));

	for (size_t i = 0; i < units.size(); ++i)
	{
		const auto& unit = units[i];
		if (unit.ignore)
			continue;

		int cellX = ((unit.position.X - minX) / gridSize).ToInt_RoundToZero();
		int cellZ = ((unit.position.Y - minZ) / gridSize).ToInt_RoundToZero();

		cellX = std::max(0, std::min(cellX, gridWidth - 1));
		cellZ = std::max(0, std::min(cellZ, gridHeight - 1));

		std::vector<size_t>& cell = spatialGrid.get(cellX, cellZ);
		if (cell.empty())
			filledCells.emplace_back(cellX, cellZ);
		cell.push_back(i);
	}
}

void UnitCollision::ComputePairPush(
	const MotionStateCompact& a,
	const MotionStateCompact& b,
	fixed dt,
	const PushConfig::RuntimeConfig& cfg,
	CFixedVector2D& outPushA,
	CFixedVector2D& outPushB,
	fixed& outSpinA,
	fixed& outSpinB,
	CFixedVector2D& outVelocityChangeA,
	CFixedVector2D& outVelocityChangeB,
	fixed& outHitSpinA,
	fixed& outHitSpinB,
	std::vector<ContactHit>& outHits,
	uint8_t& outPressureA,
	uint8_t& outPressureB)
{
	outPushA = CFixedVector2D();
	outPushB = CFixedVector2D();
	outSpinA = fixed::Zero();
	outSpinB = fixed::Zero();
	outVelocityChangeA = CFixedVector2D();
	outVelocityChangeB = CFixedVector2D();
	outHitSpinA = fixed::Zero();
	outHitSpinB = fixed::Zero();
	outPressureA = 0;
	outPressureB = 0;

	int movingPush = (a.isMoving ? 1 : 0) + (b.isMoving ? 1 : 0);
	const bool sameControlGroup = (a.controlGroup != INVALID_ENTITY && a.controlGroup == b.controlGroup);

	if (sameControlGroup)
		movingPush = 0;

	// A solid member of a formation is neither pushed by the other members nor pushes them: its place is
	// its slot. At a body's reach it would be pushed off-centre by every neighbour in the ranks, and turn
	// and drift with each push.
	if (sameControlGroup && (a.collisionShape.IsSolid() || b.collisionShape.IsSolid()))
		return;

	// A moving unit and an idle one leave each other alone, so that a passer-by doesn't shove a parked
	// formation about - except when either is solid, where letting a ship sail straight through an
	// anchored one is far worse than nudging the anchored one aside. The wake-up in the motion manager
	// then moves the idle unit off the push it receives. Only solid units qualify: cavalry and
	// animals are longer than they are wide too, but collide as circles, and a herd or a parked
	// squadron must stay as unpushable as infantry are. A charging unit is the other exception: what
	// it runs into is hit, standing still or not. Members of the same formation are never rigid to each
	// other: they ignore each other as obstructions, and one crossing the formation to its slot must
	// not be stopped by another.
	const bool rigidContact{!sameControlGroup &&
		(a.collisionShape.IsSolid() || b.collisionShape.IsSolid() || a.charging || b.charging)};
	if (movingPush == 1 && !rigidContact)
		return;

	// The bodies' radii, not their clearances: they are equal for the circular bodies almost every unit
	// has, and for a long body the radius is its half-width (see DeriveCollisionShape).
	entity_pos_t combinedClearance = (a.collisionShape.radius + b.collisionShape.radius).Multiply(PushConfig::PUSHING_CORRECTION);
	entity_pos_t maxDist = combinedClearance;
	const bool solidPair{a.collisionShape.IsSolid() && b.collisionShape.IsSolid()};

	if (!sameControlGroup) {
		maxDist = combinedClearance.Multiply(cfg.radiusMultiplier);
		// Two solid units push each other from further off (SolidMovingExtension and SolidStaticExtension in
		// pathfinder.xml): the extension is a non-solid unit's few metres, which for bodies this size is too
		// short for the soft push to turn them around before they touch and are stopped rigidly instead.
		// Solid against non-solid keeps that range: a crowd held off an elephant from further would hold
		// the elephant off just as far, since a push goes both ways.
		maxDist += movingPush ? (solidPair ? cfg.solidMovingPushExtension : cfg.movingPushExtension) :
			(solidPair ? cfg.solidStaticPushExtension : cfg.staticPushExtension);
	}

	// maxDist separates the bodies' axes, so long bodies also have to be reachable along them.
	const entity_pos_t maxCentreDist{maxDist + a.collisionShape.halfLength + b.collisionShape.halfLength};

	CFixedVector2D offset = a.position - b.position;

	// Most pairs are far apart: reject them before the square root. The (rounded down) length is never shorter than
	// either component, so this only skips pairs that the length test below would reject anyway.
	if (offset.X.Absolute() > maxCentreDist || offset.Y.Absolute() > maxCentreDist)
		return;

	// How far apart the two bodies are, which sets the strength of the push below.
	entity_pos_t offsetLength = offset.Length();

	// Where each body is pushed, which is its own centre unless it is long. The lever arms at the end
	// are measured from here, so a circular body is always pushed through its centre and never spins.
	CFixedVector2D contactA{a.position};
	CFixedVector2D contactB{b.position};

	if (a.collisionShape.IsCircle() && b.collisionShape.IsCircle())
	{
		if (offsetLength > maxDist)
			return;
	}
	else
	{
		// Long bodies push along their length rather than from their centres, so the gap and the
		// direction come from the nearest points of the two axes.
		CFixedVector2D nearestA, nearestB;
		offsetLength = ContactBetween(a.collisionShape, a.position, a.angle, b.collisionShape, b.position, b.angle,
			nearestA, nearestB);
		if (offsetLength > maxDist)
			return;

		// The nearest points sit on the axes rather than on the body's surface, which is the same thing
		// for the spin below: the difference between them runs along the push direction, and a lever
		// parallel to the push contributes no torque.
		contactA = nearestA;
		contactB = nearestB;

		// Crossing axes meet at a single point, which gives no direction to push along, so those keep
		// the centre offset. They are as overlapped as a pair can be, and the zero gap above already
		// gives them the strongest push.
		if (offsetLength > entity_pos_t::Zero())
			offset = nearestA - nearestB;
	}

	// The direction's own length, which is the gap again for circles but not for overlapping long bodies.
	const entity_pos_t directionLength{offset.Length()};

	if (directionLength <= entity_pos_t::Epsilon() * 10) {
		bool dir = a.entity % 2;
		offset.X = entity_pos_t::FromInt(dir ? 1 : 0);
		offset.Y = entity_pos_t::FromInt(dir ? 0 : 1);
		offsetLength = entity_pos_t::Epsilon() * 10;
	} else {
		// Normalize
		offset.X = offset.X / directionLength;
		offset.Y = offset.Y / directionLength;
	}

	// Full force from wherever the spread says, but never later than the bodies actually touching. The
	// spread is a fraction of maxDist, and for a body as big as a ship the extension beyond it is a small
	// fraction of the whole, so the spread alone would land well inside the bodies: two ships would sail
	// four metres into each other before the push (see MinimalForce) ever fired. combinedClearance times
	// the radius multiplier is where two bodies meet, so that floor changes nothing for a land unit,
	// whose spread already lies outside it.
	entity_pos_t combinedSpread = std::max(
		maxDist.Multiply(movingPush ? cfg.movingPushingSpread : cfg.staticPushingSpread),
		combinedClearance.Multiply(cfg.radiusMultiplier));
	entity_pos_t distanceFactor = maxDist - combinedSpread;

	// Force units that overlap a lot to have the maximum factor.
	if (distanceFactor <= entity_pos_t::Zero() || offsetLength < combinedSpread) {
		distanceFactor = PushConfig::MAX_DISTANCE_FACTOR;
	} else {
		distanceFactor = std::clamp((maxDist - offsetLength) / distanceFactor,
							entity_pos_t::Zero(),
							PushConfig::MAX_DISTANCE_FACTOR);
	}

	CFixedVector2D pushingDir = offset.Multiply(distanceFactor);

	entity_pos_t timeFactor = dt / PushConfig::PUSHING_REDUCTION_FACTOR;
	entity_pos_t maxPushing = timeFactor * PushConfig::MAX_PUSHING_MULTIPLIER;

	// Solid units push by the square root of their radius over PushConfig::REFERENCE_RADIUS, a small unit's. The
	// push is a distance per turn, tuned for small units: on a large body it is a small fraction of the
	// body, too little to stop two ships sailing into each other. The square root rather than the ratio
	// itself because a solid unit keeps its push as momentum, 1 / (1 - Drifting) turns' worth, ten at the
	// ships' 0.9. A unit on its clearance circle pushes the tuned amount, however big it is.
	if (a.collisionShape.IsSolid() || b.collisionShape.IsSolid())
	{
		const entity_pos_t sizeScale{std::max(entity_pos_t::FromInt(1),
			((a.collisionShape.radius + b.collisionShape.radius) / (PushConfig::REFERENCE_RADIUS * 2)).Sqrt())};
		timeFactor = timeFactor.Multiply(sizeScale);
		maxPushing = maxPushing.Multiply(sizeScale);
	}
	// Two solid units push each other harder, by SolidPushScale in pathfinder.xml, on top of the size
	// scale: what the soft push has to turn around before they touch is a body with momentum, and a crowd
	// of them is kept apart by the push alone. Solid against non-solid keeps the size scale: the push goes
	// both ways, and a stronger one would hold the solid unit off the crowd as much as the crowd off it.
	if (solidPair)
	{
		timeFactor = timeFactor.Multiply(cfg.solidPushScale);
		maxPushing = maxPushing.Multiply(cfg.solidPushScale);
	}

	entity_pos_t aWeightInv = entity_pos_t::FromInt(1) / a.weight;
	entity_pos_t bWeightInv = entity_pos_t::FromInt(1) / b.weight;

	outPushA = pushingDir.Multiply(std::min(b.weight.Multiply(timeFactor).Multiply(aWeightInv), maxPushing));
	outPushB = pushingDir.Multiply(-std::min(a.weight.Multiply(timeFactor).Multiply(bWeightInv), maxPushing));

	// A body shoved anywhere but through its centre turns as well as moves. The pushes above are this
	// turn's displacement, so dividing by dt gives the velocity change the spin is taken from.
	if (dt > fixed::Zero())
	{
		const fixed invDt{fixed::FromInt(1) / dt};
		outSpinA = SpinFromVelocityChange(contactA - a.position, outPushA.Multiply(invDt),
			a.collisionShape.InertiaPerMass()).Multiply(cfg.solidPushSpin);
		outSpinB = SpinFromVelocityChange(contactB - b.position, outPushB.Multiply(invDt),
			b.collisionShape.InertiaPerMass()).Multiply(cfg.solidPushSpin);
	}

	// The rigid part of a solid contact. Two solid bodies that touch while still moving into each other are
	// stopped. The speed at which they approach is taken away and split by weight, as in a
	// perfectly inelastic collision: equal bodies each lose half and go on together, a warship nudging
	// a fishing boat barely slows while the boat is carried along.
	// Unlike the soft push above, it is not built up over turns but applied in full this turn, as a
	// velocity change; the soft push then eases the remaining overlap apart. Only the approach speed
	// is taken, so bodies resting against each other get nothing and stay still, and nothing more
	// than that speed is ever taken, so it can't fling. Only an approach faster than the soft push can
	// undo counts as one (see below): a brush is left to the push.
	// Meant for the biggest units only (ships, elephants), a crowd of footmen is meant to squash together
	// and spring back, but a charge is a collision, whoever it is with.
	if (rigidContact && offsetLength <= a.collisionShape.radius + b.collisionShape.radius)
	{
		// offset is the unit vector from b to a, so a is closing when its velocity relative to b runs
		// against it.
		const fixed closing{-(a.velocity - b.velocity).Dot(offset)};
		const fixed totalWeight{a.weight + b.weight};

		// Only an approach the soft push can't turn around. The pushes above are what the two are moved
		// apart by this turn, so an approach slower than that, as a speed, is undone before the bodies
		// overlap by the push alone, and taking the speed away as well only halts and spins them: a herd
		// walking shoulder to shoulder brushes every turn, and would stop and twist at every brush. Half
		// the push rate, to leave the push its margin over pressure damping, which the impulse skips, and
		// never more than a walking pace: a light boat is shoved a long way by a hull's push, but running
		// into the hull is still running into it. A charge hit (see below) is a blow whatever the push.
		const fixed softRate{dt > fixed::Zero() ? (outPushA.Length() + outPushB.Length()) / dt : fixed::Zero()};
		const fixed brush{std::min(softRate / 2, cfg.solidBrushSpeed)};
		const bool hitByA{a.charging && closing >= a.minChargeSpeed};
		const bool hitByB{b.charging && closing >= b.minChargeSpeed};
		if (closing > fixed::Zero() && (closing > brush || hitByA || hitByB) && totalWeight > fixed::Zero())
		{
			outVelocityChangeA = offset.Multiply(closing.Multiply(b.weight / totalWeight));
			outVelocityChangeB = -offset.Multiply(closing.Multiply(a.weight / totalWeight));

			// The angular half of the same blow, about each body's own lever. This is a hit's spin, so it
			// joins the impact spin (with its own decay) rather than the push spin above.
			const fixed spinScale{ChargeConfig::Get().spinScale};
			outHitSpinA = SpinFromVelocityChange(contactA - a.position, outVelocityChangeA,
				a.collisionShape.InertiaPerMass()).Multiply(spinScale);
			outHitSpinB = SpinFromVelocityChange(contactB - b.position, outVelocityChangeB,
				b.collisionShape.InertiaPerMass()).Multiply(spinScale);

			// A charger that ran into this body fast enough has hit it. Both may be charging, and then
			// both have. The speed each side lost or gained is what the hit's damage is scaled by.
			if (hitByA)
				outHits.push_back(ContactHit{a.entity, b.entity, outVelocityChangeB.Length(), outVelocityChangeA.Length()});
			if (hitByB)
				outHits.push_back(ContactHit{b.entity, a.entity, outVelocityChangeA.Length(), outVelocityChangeB.Length()});
		}
	}

	int addedPressure = std::max(0, (PushConfig::PRESSURE_STATIC_FACTOR +
		(distanceFactor + entity_pos_t::FromInt(-2) / 3) * PushConfig::PRESSURE_DISTANCE_FACTOR)
		.Multiply(cfg.pressureStrength).ToInt_RoundToZero());

	outPressureA = std::min<uint8_t>(PushConfig::MAX_PRESSURE, static_cast<uint8_t>(addedPressure));
	outPressureB = std::min<uint8_t>(PushConfig::MAX_PRESSURE, static_cast<uint8_t>(addedPressure));
}

void UnitCollision::ComputePushes(
	const std::vector<MotionStateCompact>& units,
	fixed dt,
	Grid<std::vector<size_t>>& spatialGrid,
	std::vector<std::pair<int, int>>& filledCells,
	std::vector<CFixedVector2D>& outPushes,
	std::vector<fixed>& outSpins,
	std::vector<CFixedVector2D>& outVelocityChanges,
	std::vector<fixed>& outHitSpins,
	std::vector<ContactHit>& outHits,
	std::vector<uint8_t>& outPressures)
{
	const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();

	outHits.clear();
	if (!cfg.IsEnabled() || units.empty())
		return;

	outPushes.assign(units.size(), CFixedVector2D());
	outSpins.assign(units.size(), fixed::Zero());
	outVelocityChanges.assign(units.size(), CFixedVector2D());
	outHitSpins.assign(units.size(), fixed::Zero());
	outPressures.assign(units.size(), 0);
	// The largest single contact impulse each unit took this turn, see the end of this function.
	std::vector<fixed> largestImpulse(units.size(), fixed::Zero());

	// Cells the size of the longest push, so the 3x3 block around a unit still holds every unit that could push it
	// (see ComputePairPush: the widest pair is two units of the largest body, moving). Fixed 20 m cells would
	// search a 60x60 m neighbourhood to find pairs at most a few metres apart.
	// A long body reaches maxDist from its axis, so its centre can be that much further away again: the
	// cells are sized off the largest extent on the map, which is the radius for a circular body and half
	// the hull length for a ship. One ship therefore widens the cells for everyone, as one 'large' unit
	// already did.
	entity_pos_t maxRadius{entity_pos_t::Zero()};
	entity_pos_t maxHalfLength{entity_pos_t::Zero()};
	bool anySolid{false};
	for (const MotionStateCompact& unit : units)
	{
		if (unit.ignore)
			continue;
		maxRadius = std::max(maxRadius, unit.collisionShape.radius);
		maxHalfLength = std::max(maxHalfLength, unit.collisionShape.halfLength);
		anySolid = anySolid || unit.collisionShape.IsSolid();
	}

	// Two solid units push each other from further off (see ComputePairPush), so their cells are wider.
	entity_pos_t extension{std::max(cfg.movingPushExtension, cfg.staticPushExtension)};
	if (anySolid)
		extension = extension.Multiply(cfg.solidRangeScale);
	const entity_pos_t pushRange{(maxRadius * 2).Multiply(PushConfig::PUSHING_CORRECTION).Multiply(cfg.radiusMultiplier) +
		extension + maxHalfLength * 2};
	const int gridSize{std::max(1, pushRange.ToInt_RoundToInfinity())};

	int gridWidth, gridHeight;
	BuildSpatialGrid(units, gridSize, spatialGrid, filledCells, gridWidth, gridHeight);

	// Always the lower index as a: fixed-point products round towards minus infinity, so a pair taken
	// the other way round can come out an ulp different, and every unit's result must not depend on
	// which cell its neighbour happens to fall in.
	const auto accumulate = [&](const size_t first, const size_t second)
	{
		const size_t i{std::min(first, second)};
		const size_t j{std::max(first, second)};
		CFixedVector2D pushA, pushB, velocityChangeA, velocityChangeB;
		fixed spinA, spinB, hitSpinA, hitSpinB;
		uint8_t pressureA, pressureB;

		ComputePairPush(units[i], units[j], dt, cfg, pushA, pushB, spinA, spinB,
			velocityChangeA, velocityChangeB, hitSpinA, hitSpinB, outHits, pressureA, pressureB);

		outPushes[i] += pushA;
		outPushes[j] += pushB;
		outSpins[i] += spinA;
		outSpins[j] += spinB;
		outVelocityChanges[i] += velocityChangeA;
		outVelocityChanges[j] += velocityChangeB;
		outHitSpins[i] += hitSpinA;
		outHitSpins[j] += hitSpinB;
		outPressures[i] = std::min<uint8_t>(PushConfig::MAX_PRESSURE, outPressures[i] + pressureA);
		outPressures[j] = std::min<uint8_t>(PushConfig::MAX_PRESSURE, outPressures[j] + pressureB);
		if (!velocityChangeA.IsZero())
			largestImpulse[i] = std::max(largestImpulse[i], velocityChangeA.Length());
		if (!velocityChangeB.IsZero())
			largestImpulse[j] = std::max(largestImpulse[j], velocityChangeB.Length());
	};

	// Every pair once: the pairs within each filled cell, then the pairs between it and the four cells
	// ahead of it - the next column, at each z, and the next row. Of any two neighbouring cells exactly
	// one is ahead of the other, so no pair is visited twice and none is missed, and a unit still meets
	// everything in the 3x3 block around it, which is what the cells are sized for. Ignored units were
	// never put in the grid.
	static constexpr std::pair<int, int> ahead[]{{1, -1}, {1, 0}, {1, 1}, {0, 1}};
	for (const std::pair<int, int>& cellCoords : filledCells)
	{
		const std::vector<size_t>& cell{spatialGrid.get(cellCoords.first, cellCoords.second)};
		for (size_t p = 0; p < cell.size(); ++p)
			for (size_t q = p + 1; q < cell.size(); ++q)
				accumulate(cell[p], cell[q]);

		for (const std::pair<int, int>& offset : ahead)
		{
			const int nx{cellCoords.first + offset.first};
			const int nz{cellCoords.second + offset.second};
			if (nx < 0 || nx >= gridWidth || nz < 0 || nz >= gridHeight)
				continue;

			const std::vector<size_t>& other{spatialGrid.get(nx, nz)};
			for (const size_t i : cell)
				for (const size_t j : other)
					accumulate(i, j);
		}
	}

	// Each pair's contact impulse cancels the whole closing speed on its own, so neighbours on the same side would
	// stack and throw the unit back ever harder. Cap the sum at the largest single impulse, spin included.
	for (size_t i = 0; i < units.size(); ++i)
	{
		if (largestImpulse[i] <= fixed::Zero() || outVelocityChanges[i].CompareLength(largestImpulse[i]) <= 0)
			continue;
		const fixed scale{largestImpulse[i] / outVelocityChanges[i].Length()};
		outVelocityChanges[i] = outVelocityChanges[i].Multiply(scale);
		outHitSpins[i] = outHitSpins[i].Multiply(scale);
	}
}
