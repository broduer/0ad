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

#ifndef INCLUDED_UNITCOLLISION
#define INCLUDED_UNITCOLLISION

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/helpers/Grid.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/helpers/PushConfig.h"
#include "simulation2/system/Entity.h"

#include <utility>
#include <vector>
#include <cstdint>

/**
 * Standalone push calculator - no component dependencies.
 * All push logic lives here, separate from any component.
 */
class UnitCollision
{
public:
	/**
	 * A unit's body: a segment of 2 * halfLength along one of the entity's axes, inflated by radius.
	 * A halfLength of zero makes it a plain circle, which is what most units are.
	 *
	 * A body with a segment - a long body - is what a solid unit gets from its footprint (PhysicalBody/Solid,
	 * see DeriveCollisionShape). Several rules treat long bodies differently from circles, because a 40 m body
	 * passing through another is far more visible than a cavalryman brushing a footman: they push and are
	 * pushed by idle units, turn when pushed off their centre, are stopped rigidly by what they run into,
	 * and pass other units' obstruction shapes. They also cost more to push, since every pair one is in
	 * measures the distance between two segments rather than two points.
	 */
	struct CollisionShape
	{
		entity_pos_t radius;
		entity_pos_t halfLength;
		// Whether the axis runs along the entity's width (its local X) rather than its depth (local Z).
		bool alongWidth;
		// A solid unit (PhysicalBody/Solid) collides as its footprint outline: it is stopped rigidly by
		// what it runs into, turns when struck off its centre, nudges idle units aside, and passes other
		// units' obstruction shapes. Everything else is a circle of its clearance that crowds squash
		// together.
		bool solid;

		// A shape with no length. What matters for the geometry: a circle has no axis, no lever, and no
		// point of contact off its centre. Not the same as not being solid: a solid unit with a square
		// footprint is a circle here, and solid all the same.
		bool IsCircle() const { return halfLength <= entity_pos_t::Zero(); }
		bool IsSolid() const { return solid; }
		// How far the body reaches from its centre, in the direction it reaches furthest.
		entity_pos_t Extent() const { return radius + halfLength; }

		/**
		 * The body's moment of inertia per unit of mass, which is its squared radius of gyration.
		 * Taking the body as a rectangle of 2 * radius by 2 * Extent() about its centre gives
		 * (radius^2 + Extent()^2) / 3, so a long body resists being spun far more than a round unit
		 * of the same size does.
		 */
		entity_pos_t InertiaPerMass() const;

		/**
		 * The body's axis as a segment in world space, for an entity at @p position facing @p angle.
		 * Both ends are the position itself for a circular body, which makes every distance taken
		 * against it a plain centre-to-centre one.
		 */
		void GetSegment(const CFixedVector2D& position, entity_angle_t angle,
			CFixedVector2D& out0, CFixedVector2D& out1) const;
	};

	/**
	 * The body a footprint gives: half its width for the radius, the rest of its length for the axis, so
	 * that what the unit collides with is exactly the outline drawn under it when selected. A square
	 * footprint (or a circular one, given as its diameter twice) has nothing to stretch along and is a
	 * circle of half its side.
	 *
	 * Only units whose PhysicalBody asks for it (CollisionShape footprint - the ships) collide as their
	 * footprint. Everyone else is the circle of their pathfinding clearance, which is far cheaper to push
	 * and what the pushing distances in pathfinder.xml are tuned for.
	 */
	static CollisionShape DeriveCollisionShape(entity_pos_t footprintWidth, entity_pos_t footprintDepth);

	/**
	 * The spin, in radians per second, that a hit gives a body struck at @p lever from its centre.
	 *
	 * This is the angular half of an impulse: a body struck off-centre turns as well as moves, by
	 * omega = cross(lever, impulse) / inertia. It is written in terms of the velocity change rather than
	 * the impulse - cross(lever, impulse / mass) / (inertia / mass) is the same thing - so that callers
	 * can reuse the weight damping they already apply to the linear half.
	 *
	 * A lever of zero, which is what every circular body has, gives no spin at all: a push through the
	 * centre of mass cannot turn anything.
	 */
	static fixed SpinFromVelocityChange(const CFixedVector2D& lever, const CFixedVector2D& velocityChange,
		entity_pos_t inertiaPerMass);

	/**
	 * The velocity of a point of a body at @p lever from its centre: the centre's @p velocity plus what its turning
	 * at @p angularVelocity adds there.
	 */
	static CFixedVector2D PointVelocity(const CFixedVector2D& velocity, fixed angularVelocity, const CFixedVector2D& lever);

	/**
	 * Where two bodies meet: the nearest points of their axes, in @p outContactA and @p outContactB, and
	 * the distance between those axes. Each contact is the lever its body is turned about, and the line
	 * between them is the direction the bodies push each other along. A circular body's contact is its
	 * own centre, so it is never turned by anything.
	 */
	static fixed ContactBetween(
		const CollisionShape& a, const CFixedVector2D& positionA, entity_angle_t angleA,
		const CollisionShape& b, const CFixedVector2D& positionB, entity_angle_t angleB,
		CFixedVector2D& outContactA, CFixedVector2D& outContactB);

	/**
	 * Compact state structure for efficient batch processing.
	 * Contains only the data needed for push calculations (see ComputePushes).
	 */
	struct MotionStateCompact
	{
		entity_id_t entity;
		CFixedVector2D position;
		entity_angle_t angle;
		// How the unit actually moved last turn, in m/s: what the contact impulse reads closing speeds from.
		CFixedVector2D velocity;
		// How fast the unit is turning, in radians per second (positive turns +Z towards +X, see
		// CFixedVector2D::Rotate). The approach between two bodies is measured where they touch, and a long
		// body's contact point moves with its turning as well as with its centre (see PointVelocity).
		fixed angularVelocity;
		entity_pos_t clearance;
		CollisionShape collisionShape;
		entity_pos_t weight;
		bool isMoving;
		bool ignore;
		entity_id_t controlGroup;
		// Moving fast enough that running into something is a charge (see ContactHit): its contacts are
		// always found, and reported with the momentum they exchanged.
		bool charging;
		entity_pos_t minChargeSpeed;
		// What ground the unit can stand on, for the room a push on it has (see PushRoom).
		pass_class_t passClass;

		void GetSegment(CFixedVector2D& out0, CFixedVector2D& out1) const
		{
			collisionShape.GetSegment(position, angle, out0, out1);
		}
	};

	/**
	 * A charging unit has run into another: the two bodies touched while closing at least as fast as
	 * the charger's minimum charge speed. The speed changes are what the contact impulse gave each side,
	 * in m/s, which is what the damage and stagger of the hit are scaled by.
	 */
	struct ContactHit
	{
		entity_id_t charger;
		entity_id_t target;
		fixed targetSpeedChange;
		fixed chargerSpeedChange;
	};

	/**
	 * A solid unit within push range of another solid unit: the unit's index, the contact's unit normal
	 * pointing away from the other body, and the gap left between them (zero once they touch).
	 */
	struct Contact
	{
		size_t unit;
		CFixedVector2D normal;
		entity_pos_t gap;
		// How fast the other body is moving away from the unit along the normal, in m/s (zero if it isn't).
		fixed receding;
	};

	/**
	 * One tier of the spatial grids: the units whose reach (see ComputePushes) falls in its band, sorted into
	 * cells sized for them. Kept by the caller between turns: only the cells filled last turn are cleared,
	 * and the grid is only resized when its shape changes. The cell (x, z) covers
	 * [minX + x * cellSize, minX + (x + 1) * cellSize) by the same in z, and filledCells lists the cells
	 * holding at least one unit, in the order they were first filled.
	 */
	struct SpatialGridTier
	{
		Grid<std::vector<size_t>> grid;
		std::vector<std::pair<int, int>> filledCells;
		entity_pos_t minX;
		entity_pos_t minZ;
		entity_pos_t cellSize;
		int width = 0;
		int height = 0;
		// The longest reach among this turn's members, which is half the cell size.
		entity_pos_t maxReach;
		// Whether any unit fell in this tier this turn.
		bool used = false;
	};

	/**
	 * How much ground the units take up: the footprint area that falls in each cell. A soft push between two solid
	 * units is scaled by the room where it would put the pushed unit, read from a window of cells the size of its
	 * body just beyond its edge along the push, leaving the pair itself out and counting ground the pushed unit
	 * cannot stand on as taken (see PushRoom).
	 */
	struct DensityGrid
	{
		entity_pos_t minX;
		entity_pos_t minZ;
		entity_pos_t cellSize;
		int width = 0;
		int height = 0;
		// Footprint area in each cell, in square metres.
		std::vector<entity_pos_t> area;
		// Each unit's cell, or -1 for a unit that is not on the grid.
		std::vector<int> cellOf;
	};

	/**
	 * The spatial grids of the pushing pass, one tier per band of body reach: tier 0 holds bodies reaching
	 * up to TIER_BASE_REACH, and each tier above doubles that. Infantry and cavalry are tier 0, elephants
	 * and siege engines tier 1, ships tier 2 or 3. Each tier's cells fit its own members, so a ship never
	 * widens the cells the infantry are in; see ComputePushes for how the tiers meet. The density grid
	 * beside them is what the room for a push is read from.
	 */
	struct SpatialGrids
	{
		std::vector<SpatialGridTier> tiers;
		DensityGrid density;
	};

	/**
	 * The ground a body's footprint takes up, in square metres.
	 */
	static entity_pos_t FootprintArea(const CollisionShape& shape);

	/**
	 * Fill @p grid with every unit's footprint area, in cells of @p cellSize over the box the units span plus a
	 * cell's margin, so a spot just beyond the last unit still reads as open ground.
	 */
	static void BuildDensityGrid(const std::vector<MotionStateCompact>& units, const CFixedVector2D& minCorner,
		const CFixedVector2D& maxCorner, entity_pos_t cellSize, DensityGrid& grid);

	/**
	 * How much room a push on @p pushed has, from @p floor (full ground) to 1 (open ground): the share of the ground
	 * the body would move into that is free of units other than the pair. That ground is a square window of cells
	 * centred just beyond the body's edge along @p direction (a unit vector), reaching as many cells to each side
	 * as the body's extent spans, one at least. Ground impassable to @p pushed (in @p passability, when given)
	 * counts as taken too, so a push towards a wall or a tree counts like a push into a crowd. The ground is full
	 * once @p fullCoverage of it is taken (footprints may overlap, so the sum can pass 1). Even full ground leaves
	 * @p floor of the push: without it, bodies in a packed crowd would have only the overlap correction to keep
	 * them apart, and sink into each other.
	 */
	static fixed PushRoom(const DensityGrid& grid, const std::vector<MotionStateCompact>& units, size_t pushed,
		size_t pusher, const CFixedVector2D& direction, entity_pos_t fullCoverage, fixed floor,
		const Grid<NavcellData>* passability);

	static constexpr entity_pos_t TIER_BASE_REACH = entity_pos_t::FromInt(4);

	/**
	 * Compute pushes for all units in one batch.
	 * @param units - compact state of all units
	 * @param dt - timestep
	 * @param grids - kept by the caller between turns, see SpatialGrids.
	 * @param outPushes - output push vectors for each unit
	 * @param outSpins - output spin, in radians per second, for each unit. Always zero for a circular
	 *        body, which cannot be pushed off its own centre.
	 * @param outVelocityChanges - output contact impulses, as velocity changes in m/s, for each unit: the
	 *        rigid half of a contact, which stops two touching bodies closing on each other at once
	 *        (see ComputePairPush). Zero unless a solid or a charging unit is involved.
	 * @param outHitSpins - the spin (radians/second) those contact impulses give each unit: a hit's
	 *        angular half, integrated with impact spin rather than push spin.
	 * @param outHits - every contact that is a charge hit this turn, see ContactHit.
	 * @param outPressures - output pressure increases for each unit
	 * @param outCorrections - how far each solid unit is moved out of the solid units it overlaps this turn, in
	 *        metres: a displacement for this turn only, never carried on as momentum (see ComputePairPush).
	 * @param outContacts - if given, every solid unit touching another solid unit this turn, or about to
	 *        (see Contact), one entry per unit per contact.
	 */
	static void ComputePushes(
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
		const Grid<NavcellData>* passability = nullptr,
		std::vector<Contact>* outContacts = nullptr);

private:
	/**
	 * Sort the units of tier @p tierIndex into that tier's cells, sized to twice its longest reach. The grid
	 * spans @p minCorner to @p maxCorner - every unit on the map, not only this tier's - with a cell of slack
	 * around it, so that a unit of a higher tier scanning this grid is always inside it, and a unit exactly
	 * on the edge still has its neighbouring cells inside. An unused tier is only cleared.
	 */
	static void BuildSpatialGrid(const std::vector<MotionStateCompact>& units, const std::vector<int>& tierOf,
								  int tierIndex, const CFixedVector2D& minCorner, const CFixedVector2D& maxCorner,
								  SpatialGridTier& tier);

	static void ComputePairPush(
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
		Contact* outContact = nullptr);
};

#endif // INCLUDED_UNITCOLLISION