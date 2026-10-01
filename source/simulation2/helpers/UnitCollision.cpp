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
#include "ps/CLogger.h"

// Logs every pair the pushing pass acts on that involves a solid unit: who pushed whom by how much, the
// contact impulse if any, and how far apart their centres were. Says where a solid unit's shoves come from.
#define DEBUG_PAIR_TRACE 0

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

entity_pos_t UnitCollision::FootprintArea(const CollisionShape& shape)
{
	const entity_pos_t width{shape.radius * 2};
	if (shape.IsCircle())
		return width.Multiply(width).Multiply(entity_pos_t::FromFraction(11, 14));  // pi/4
	return width.Multiply(width + shape.halfLength * 2);
}

void UnitCollision::BuildDensityGrid(const std::vector<MotionStateCompact>& units, const CFixedVector2D& minCorner,
	const CFixedVector2D& maxCorner, const entity_pos_t cellSize, DensityGrid& grid)
{
	grid.cellSize = cellSize;
	grid.minX = minCorner.X - cellSize;
	grid.minZ = minCorner.Y - cellSize;
	grid.width = ((maxCorner.X - grid.minX) / cellSize).ToInt_RoundToZero() + 2;
	grid.height = ((maxCorner.Y - grid.minZ) / cellSize).ToInt_RoundToZero() + 2;
	grid.area.assign(static_cast<size_t>(grid.width) * grid.height, entity_pos_t::Zero());
	grid.cellOf.assign(units.size(), -1);
	for (size_t i = 0; i < units.size(); ++i)
	{
		if (units[i].ignore)
			continue;
		const int cellX{Clamp(((units[i].position.X - grid.minX) / cellSize).ToInt_RoundToZero(), 0, grid.width - 1)};
		const int cellZ{Clamp(((units[i].position.Y - grid.minZ) / cellSize).ToInt_RoundToZero(), 0, grid.height - 1)};
		grid.cellOf[i] = cellZ * grid.width + cellX;
		grid.area[grid.cellOf[i]] += FootprintArea(units[i].collisionShape);
	}
}

/**
 * The ground in the density grid cell at (@p cellX, @p cellZ) that @p passClass cannot stand on, in square metres:
 * its impassable navcells. Beyond the passability grid is beyond the map: all of it.
 */
static entity_pos_t ImpassableArea(const UnitCollision::DensityGrid& grid, const int cellX, const int cellZ,
	const pass_class_t passClass, const Grid<NavcellData>& passability)
{
	const int i0{((grid.minX + grid.cellSize * cellX) / Pathfinding::NAVCELL_SIZE).ToInt_RoundToNegInfinity()};
	const int j0{((grid.minZ + grid.cellSize * cellZ) / Pathfinding::NAVCELL_SIZE).ToInt_RoundToNegInfinity()};
	const int navcells{(grid.cellSize / Pathfinding::NAVCELL_SIZE).ToInt_RoundToInfinity()};
	int impassable{0};
	for (int j = j0; j < j0 + navcells; ++j)
		for (int i = i0; i < i0 + navcells; ++i)
			if (i < 0 || j < 0 || i >= passability.width() || j >= passability.height() ||
				!IS_PASSABLE(passability.get(i, j), passClass))
				++impassable;
	return Pathfinding::NAVCELL_SIZE.Multiply(Pathfinding::NAVCELL_SIZE) * impassable;
}

fixed UnitCollision::PushRoom(const DensityGrid& grid, const std::vector<MotionStateCompact>& units, const size_t pushed,
	const size_t pusher, const CFixedVector2D& direction, const entity_pos_t fullCoverage, const fixed floor,
	const Grid<NavcellData>* passability)
{
	// The ground the body would move into: a window its own size, just beyond its edge along the push.
	const entity_pos_t extent{units[pushed].collisionShape.Extent()};
	const CFixedVector2D probe{units[pushed].position + direction.Multiply(extent + grid.cellSize / 2)};
	const int cellX{((probe.X - grid.minX) / grid.cellSize).ToInt_RoundToNegInfinity()};
	const int cellZ{((probe.Y - grid.minZ) / grid.cellSize).ToInt_RoundToNegInfinity()};
	const int reach{std::max(1, (extent / grid.cellSize).ToInt_RoundToNearest())};

	// The grid spans the units, with a cell to spare: beyond it is open ground as far as they go, but the
	// passability is sampled there all the same.
	entity_pos_t area;
	const auto inWindow = [&](const int cell) {
		return cell >= 0 && std::abs(cell % grid.width - cellX) <= reach && std::abs(cell / grid.width - cellZ) <= reach;
	};
	for (int z = cellZ - reach; z <= cellZ + reach; ++z)
		for (int x = cellX - reach; x <= cellX + reach; ++x)
		{
			if (x >= 0 && z >= 0 && x < grid.width && z < grid.height)
				area += grid.area[z * grid.width + x];
			if (passability)
				area += ImpassableArea(grid, x, z, units[pushed].passClass, *passability);
		}
	if (inWindow(grid.cellOf[pushed]))
		area -= FootprintArea(units[pushed].collisionShape);
	if (inWindow(grid.cellOf[pusher]))
		area -= FootprintArea(units[pusher].collisionShape);
	if (area <= entity_pos_t::Zero())
		return fixed::FromInt(1);

	const entity_pos_t full{grid.cellSize.Multiply(grid.cellSize).Multiply(fullCoverage) * ((2 * reach + 1) * (2 * reach + 1))};
	if (area >= full)
		return floor;
	return floor + (fixed::FromInt(1) - floor).Multiply(fixed::FromInt(1) - area / full);
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
	// so the sign is flipped here to give a spin that can be added to the angle as it is.
	// Both are short relative vectors - the lever is bounded by the body's extent, a few tens of metres
	// at most, and a velocity change by a few metres per second - so their products stay well inside
	// what Dot can hold.
	const fixed torque{velocityChange.Dot(lever.Perpendicular())};
	return torque / inertiaPerMass;
}

CFixedVector2D UnitCollision::PointVelocity(const CFixedVector2D& velocity, const fixed angularVelocity,
	const CFixedVector2D& lever)
{
	// Turning by a small angle moves a point at (x, z) by (z, -x) times that angle (see CFixedVector2D::Rotate),
	// which is lever.Perpendicular().
	if (angularVelocity.IsZero() || lever.IsZero())
		return velocity;
	return velocity + lever.Perpendicular().Multiply(angularVelocity);
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

void UnitCollision::BuildSpatialGrid(const std::vector<MotionStateCompact>& units, const std::vector<int>& tierOf,
									   const int tierIndex, const CFixedVector2D& minCorner, const CFixedVector2D& maxCorner,
									   SpatialGridTier& tier)
{
	// Only the cells we filled last turn can hold anything, so clearing those is proportional to the units,
	// not to the map: with cells sized to the push range there are far more cells than units.
	for (const std::pair<int, int>& cell : tier.filledCells)
		tier.grid.get(cell.first, cell.second).clear();
	tier.filledCells.clear();

	if (!tier.used)
		return;

	const int gridSize{std::max(1, (tier.maxReach * 2).ToInt_RoundToInfinity())};
	const entity_pos_t margin{entity_pos_t::FromInt(gridSize)};
	tier.minX = minCorner.X - margin;
	tier.minZ = minCorner.Y - margin;
	tier.cellSize = entity_pos_t::FromInt(gridSize);
	tier.width = std::max(1, ((maxCorner.X + margin - tier.minX) / gridSize).ToInt_RoundToNearest() + 1);
	tier.height = std::max(1, ((maxCorner.Y + margin - tier.minZ) / gridSize).ToInt_RoundToNearest() + 1);

	// Resizing reallocates every cell, so only do it when the grid actually changes shape.
	if (tier.grid.width() != tier.width || tier.grid.height() != tier.height)
		tier.grid.resize(static_cast<std::uint16_t>(tier.width), static_cast<std::uint16_t>(tier.height));

	for (size_t i = 0; i < units.size(); ++i)
	{
		if (tierOf[i] != tierIndex)
			continue;

		const int cellX{Clamp(((units[i].position.X - tier.minX) / gridSize).ToInt_RoundToZero(), 0, tier.width - 1)};
		const int cellZ{Clamp(((units[i].position.Y - tier.minZ) / gridSize).ToInt_RoundToZero(), 0, tier.height - 1)};

		std::vector<size_t>& cell{tier.grid.get(cellX, cellZ)};
		if (cell.empty())
			tier.filledCells.emplace_back(cellX, cellZ);
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
	uint8_t& outPressureB,
	CFixedVector2D& outCorrectionA,
	CFixedVector2D& outCorrectionB,
	Contact* outContact)
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
	outCorrectionA = CFixedVector2D();
	outCorrectionB = CFixedVector2D();
	if (outContact)
		outContact->normal = CFixedVector2D();
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

	// Two solid bodies in push range are a contact: their steering closes on it no faster than its gap allows.
	if (outContact && rigidContact && solidPair)
	{
		outContact->normal = offset;
		outContact->gap = std::max(offsetLength - a.collisionShape.radius - b.collisionShape.radius, entity_pos_t::Zero());
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
		// against it. Measured where the bodies touch, not at their centres, so a hull turning into its
		// neighbour closes on it too.
		const fixed closing{-(PointVelocity(a.velocity, a.angularVelocity, contactA - a.position) -
			PointVelocity(b.velocity, b.angularVelocity, contactB - b.position)).Dot(offset)};
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

	// Two solid bodies that overlap are moved apart by a share of how deep they overlap, split by weight like the
	// contact impulse. The soft push is the same however deep the overlap, and the contact impulse only acts on an
	// approach, so without this a deep overlap that has stopped closing would only be eased out at the soft push's
	// rate, against whatever keeps driving the bodies in. A displacement for this turn only: carried on as
	// momentum, a deep overlap would fling the bodies apart.
	if (rigidContact && solidPair && offsetLength < a.collisionShape.radius + b.collisionShape.radius)
	{
		const fixed totalWeight{a.weight + b.weight};
		const fixed share{fixed::FromInt(1) - PushConfig::DecayFactor(fixed::FromInt(1) - cfg.solidOverlapCorrection, dt)};
		if (totalWeight > fixed::Zero() && share > fixed::Zero())
		{
			const fixed correction{(a.collisionShape.radius + b.collisionShape.radius - offsetLength).Multiply(share)};
			outCorrectionA = offset.Multiply(correction.Multiply(b.weight / totalWeight));
			outCorrectionB = -offset.Multiply(correction.Multiply(a.weight / totalWeight));
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
	SpatialGrids& grids,
	std::vector<CFixedVector2D>& outPushes,
	std::vector<fixed>& outSpins,
	std::vector<CFixedVector2D>& outVelocityChanges,
	std::vector<fixed>& outHitSpins,
	std::vector<ContactHit>& outHits,
	std::vector<uint8_t>& outPressures,
	std::vector<CFixedVector2D>& outCorrections,
	const Grid<NavcellData>* passability,
	std::vector<Contact>* outContacts)
{
	const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();

	outHits.clear();
	if (outContacts)
		outContacts->clear();
	if (!cfg.IsEnabled() || units.empty())
		return;

	outPushes.assign(units.size(), CFixedVector2D());
	outSpins.assign(units.size(), fixed::Zero());
	outVelocityChanges.assign(units.size(), CFixedVector2D());
	outHitSpins.assign(units.size(), fixed::Zero());
	outPressures.assign(units.size(), 0);
	outCorrections.assign(units.size(), CFixedVector2D());
	// The largest single overlap correction each unit got this turn, see the end of this function.
	std::vector<fixed> largestCorrection(units.size(), fixed::Zero());
	// The largest single contact impulse each unit took this turn, see the end of this function.
	std::vector<fixed> largestImpulse(units.size(), fixed::Zero());
	std::vector<fixed> largestPush(units.size(), fixed::Zero());

	// How far from its centre a body can push: what ComputePairPush allows a pair is the sum of the two.
	// Each unit goes in the tier for its reach (see SpatialGrids), and the grid spans every unit.
	const entity_pos_t halfExtension{std::max(cfg.movingPushExtension, cfg.staticPushExtension) / 2};
	const entity_pos_t solidHalfExtension{std::max(cfg.solidMovingPushExtension, cfg.solidStaticPushExtension) / 2};
	std::vector<entity_pos_t> reaches(units.size());
	std::vector<int> tierOf(units.size(), -1);
	int tierCount{0};
	CFixedVector2D minCorner, maxCorner;
	bool any{false};
	for (size_t i = 0; i < units.size(); ++i)
	{
		const MotionStateCompact& unit{units[i]};
		if (unit.ignore)
			continue;

		reaches[i] = unit.collisionShape.radius.Multiply(PushConfig::PUSHING_CORRECTION).Multiply(cfg.radiusMultiplier) +
			unit.collisionShape.halfLength +
			(unit.collisionShape.IsSolid() ? solidHalfExtension : halfExtension);
		int tier{0};
		for (entity_pos_t band{TIER_BASE_REACH}; reaches[i] > band; band = band * 2)
			++tier;
		tierOf[i] = tier;
		tierCount = std::max(tierCount, tier + 1);

		if (!any)
		{
			minCorner = maxCorner = unit.position;
			any = true;
		}
		minCorner.X = std::min(minCorner.X, unit.position.X);
		minCorner.Y = std::min(minCorner.Y, unit.position.Y);
		maxCorner.X = std::max(maxCorner.X, unit.position.X);
		maxCorner.Y = std::max(maxCorner.Y, unit.position.Y);
	}
	if (!any)
		return;

	// Tiers are never dropped, so their grids stay allocated for the next ship that comes along.
	if (grids.tiers.size() < static_cast<size_t>(tierCount))
		grids.tiers.resize(tierCount);
	for (SpatialGridTier& tier : grids.tiers)
	{
		tier.maxReach = entity_pos_t::Zero();
		tier.used = false;
	}
	for (size_t i = 0; i < units.size(); ++i)
		if (tierOf[i] >= 0)
		{
			SpatialGridTier& tier{grids.tiers[tierOf[i]]};
			tier.maxReach = std::max(tier.maxReach, reaches[i]);
			tier.used = true;
		}
	for (size_t t = 0; t < grids.tiers.size(); ++t)
		BuildSpatialGrid(units, tierOf, static_cast<int>(t), minCorner, maxCorner, grids.tiers[t]);
	const bool pushNeedsRoom{cfg.densityPushCoverage > entity_pos_t::Zero() && cfg.densityCellSize > entity_pos_t::Zero()};
	if (pushNeedsRoom)
		BuildDensityGrid(units, minCorner, maxCorner, cfg.densityCellSize, grids.density);

	// Always the lower index as a: fixed-point products round towards minus infinity, so a pair taken
	// the other way round can come out an ulp different, and every unit's result must not depend on
	// which cell its neighbour happens to fall in.
	const auto accumulate = [&](const size_t first, const size_t second)
	{
		const size_t i{std::min(first, second)};
		const size_t j{std::max(first, second)};
		CFixedVector2D pushA, pushB, velocityChangeA, velocityChangeB, correctionA, correctionB;
		fixed spinA, spinB, hitSpinA, hitSpinB;
		uint8_t pressureA, pressureB;

		Contact contact{};
		ComputePairPush(units[i], units[j], dt, cfg, pushA, pushB, spinA, spinB,
			velocityChangeA, velocityChangeB, hitSpinA, hitSpinB, outHits, pressureA, pressureB, correctionA, correctionB,
			outContacts ? &contact : nullptr);
		if (!contact.normal.IsZero())
		{
			// The normal points from j to i: j moves away from i when its velocity runs against it, and i
			// away from j when its own runs along it.
			outContacts->push_back({i, contact.normal, contact.gap,
				std::max(fixed::Zero(), -units[j].velocity.Dot(contact.normal))});
			outContacts->push_back({j, -contact.normal, contact.gap,
				std::max(fixed::Zero(), units[i].velocity.Dot(contact.normal))});
		}

		// A soft push between solid bodies moves a unit only as far as there is room where the push leads. The
		// push is applied in full on open ground, and scaled down as the ground fills, to DensityPushFloor of it
		// once units and ground the pushed unit cannot stand on cover DensityPushCoverage of it. With others
		// behind the pushed unit, the push would only be passed on to them: a herd at rest would keep making
		// space in ripples long after a few more had joined it, until every pair was out of push range. Against
		// a wall or a tree, it would only press the pushed unit into them.
		if (pushNeedsRoom && units[i].collisionShape.IsSolid() && units[j].collisionShape.IsSolid())
		{
			const auto scaleToRoom = [&](CFixedVector2D& push, fixed& spin, const size_t pushed, const size_t pusher) {
				if (push.IsZero())
					return;
				CFixedVector2D direction{push};
				direction.Normalize();
				const fixed room{PushRoom(grids.density, units, pushed, pusher, direction, cfg.densityPushCoverage,
					cfg.densityPushFloor, passability)};
				push = push.Multiply(room);
				spin = spin.Multiply(room);
			};
			scaleToRoom(pushA, spinA, i, j);
			scaleToRoom(pushB, spinB, j, i);
		}
#if DEBUG_PAIR_TRACE
		if ((units[i].collisionShape.IsSolid() || units[j].collisionShape.IsSolid()) &&
			(pushA.CompareLength(fixed::FromFraction(1, 10)) > 0 || pushB.CompareLength(fixed::FromFraction(1, 10)) > 0 ||
			!velocityChangeA.IsZero() || !velocityChangeB.IsZero()))
			LOGWARNING("Pair trace: %u(%s%s) - %u(%s%s): push %.2f / %.2f m, impulse %.2f / %.2f m/s, overlap correction %.2f / %.2f m, centres %.1f m apart",
				units[i].entity, units[i].isMoving ? "moving" : "idle", units[i].collisionShape.IsSolid() ? " solid" : "",
				units[j].entity, units[j].isMoving ? "moving" : "idle", units[j].collisionShape.IsSolid() ? " solid" : "",
				pushA.Length().ToDouble(), pushB.Length().ToDouble(),
				velocityChangeA.Length().ToDouble(), velocityChangeB.Length().ToDouble(),
				correctionA.Length().ToDouble(), correctionB.Length().ToDouble(),
				(units[i].position - units[j].position).Length().ToDouble());
#endif

		outPushes[i] += pushA;
		outPushes[j] += pushB;
		outSpins[i] += spinA;
		outSpins[j] += spinB;
		if (units[i].collisionShape.IsSolid())
			largestPush[i] = std::max(largestPush[i], pushA.Length());
		if (units[j].collisionShape.IsSolid())
			largestPush[j] = std::max(largestPush[j], pushB.Length());
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
		if (!correctionA.IsZero())
		{
			outCorrections[i] += correctionA;
			largestCorrection[i] = std::max(largestCorrection[i], correctionA.Length());
		}
		if (!correctionB.IsZero())
		{
			outCorrections[j] += correctionB;
			largestCorrection[j] = std::max(largestCorrection[j], correctionB.Length());
		}
	};

	// Within a tier, every pair once: the pairs within each filled cell, then the pairs between it and the
	// four cells ahead of it - the next column, at each z, and the next row. Of any two neighbouring cells
	// exactly one is ahead of the other, so no pair is visited twice and none is missed, and a unit still
	// meets everything in the 3x3 block around it, which is what the tier's cells are sized for.
	static constexpr std::pair<int, int> ahead[]{{1, -1}, {1, 0}, {1, 1}, {0, 1}};
	for (const SpatialGridTier& tier : grids.tiers)
	{
		if (!tier.used)
			continue;

		for (const std::pair<int, int>& cellCoords : tier.filledCells)
		{
			const std::vector<size_t>& cell{tier.grid.get(cellCoords.first, cellCoords.second)};
			for (size_t p = 0; p < cell.size(); ++p)
				for (size_t q = p + 1; q < cell.size(); ++q)
					accumulate(cell[p], cell[q]);

			for (const std::pair<int, int>& offset : ahead)
			{
				const int nx{cellCoords.first + offset.first};
				const int nz{cellCoords.second + offset.second};
				if (nx < 0 || nx >= tier.width || nz < 0 || nz >= tier.height)
					continue;

				const std::vector<size_t>& other{tier.grid.get(nx, nz)};
				for (const size_t i : cell)
					for (const size_t j : other)
						accumulate(i, j);
			}
		}
	}

	// Between tiers, the unit of the higher tier does the looking: it scans, in each lower tier's grid, the
	// block of cells within its own reach plus that tier's longest reach - as far as any unit there that
	// could touch it can be. Only the higher unit scans, so each such pair is found exactly once.
	for (size_t i = 0; i < units.size(); ++i)
	{
		if (tierOf[i] <= 0)
			continue;

		for (int t = 0; t < tierOf[i]; ++t)
		{
			const SpatialGridTier& tier{grids.tiers[t]};
			if (!tier.used)
				continue;

			const entity_pos_t scanRange{reaches[i] + tier.maxReach};
			const int cells{(scanRange / tier.cellSize).ToInt_RoundToInfinity()};
			const int cellX{Clamp(((units[i].position.X - tier.minX) / tier.cellSize).ToInt_RoundToZero(), 0, tier.width - 1)};
			const int cellZ{Clamp(((units[i].position.Y - tier.minZ) / tier.cellSize).ToInt_RoundToZero(), 0, tier.height - 1)};

			for (int nx = std::max(0, cellX - cells); nx <= std::min(tier.width - 1, cellX + cells); ++nx)
				for (int nz = std::max(0, cellZ - cells); nz <= std::min(tier.height - 1, cellZ + cells); ++nz)
					for (const size_t j : tier.grid.get(nx, nz))
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

	// A solid unit is only pushed as hard as its strongest pusher: added up, a few neighbours on one side would
	// drive it through whatever is on the other. A crowd of footmen squashes instead.
	// The pushes are still summed first, so pushes from opposite sides cancel as before: a squeezed unit isn't
	// handed one side's push, and the cap only shortens a sum of pushes that all lean the same way.
	for (size_t i = 0; i < units.size(); ++i)
	{
		if (largestPush[i] <= fixed::Zero() || outPushes[i].CompareLength(largestPush[i]) <= 0)
			continue;
		const fixed scale{largestPush[i] / outPushes[i].Length()};
		outPushes[i] = outPushes[i].Multiply(scale);
		outSpins[i] = outSpins[i].Multiply(scale);
	}

	// Likewise the overlap corrections: two neighbours on the same side each want the unit moved out of their own
	// overlap, and added up they would move it twice as far.
	for (size_t i = 0; i < units.size(); ++i)
		if (largestCorrection[i] > fixed::Zero() && outCorrections[i].CompareLength(largestCorrection[i]) > 0)
			outCorrections[i] = outCorrections[i].Multiply(largestCorrection[i] / outCorrections[i].Length());
}
