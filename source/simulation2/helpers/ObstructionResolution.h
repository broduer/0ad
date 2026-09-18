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

#ifndef INCLUDED_OBSTRUCTIONRESOLUTION
#define INCLUDED_OBSTRUCTIONRESOLUTION

#include "maths/FixedVector2D.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/system/Entity.h"

class ICmpPathfinder;
class IObstructionTestFilter;

/**
 * What a unit does when its move for this turn is obstructed: find a nearby position it can move to instead.
 * Stateless: both functions only move pos, and only to a position the pathfinder accepts from pos.
 */
class ObstructionResolution
{
public:
	/**
	 * For units with no path to follow (pushed, drifting, or waiting for a path): slide along the obstacle,
	 * trying the movement projected onto +45° and -45°.
	 * Each unit consistently tries the same side first (chosen by its entity id), so it doesn't flip-flop
	 * between turns, and a crowd doesn't drift one way.
	 * @return true if pos was moved.
	 */
	static bool TrySlide(CFixedVector2D& pos, const CFixedVector2D& movement, const entity_id_t entityId,
		const ICmpPathfinder& pathfinder, const IObstructionTestFilter& filter,
		const entity_pos_t clearance, const pass_class_t passClass);

	/**
	 * For units following a path: step sideways around the obstacle, 0.5 to 2 navcells left or right, the
	 * waypoint's side first, first together with the movement, then (from 1 navcell) instead of it.
	 * A candidate position only counts if the unit can still move straight from it to its waypoint.
	 * @return true if pos was moved.
	 */
	static bool TryPerpendicularNudge(CFixedVector2D& pos, const CFixedVector2D& movement, const CFixedVector2D& waypoint,
		const ICmpPathfinder& pathfinder, const IObstructionTestFilter& filter,
		const entity_pos_t clearance, const pass_class_t passClass);
};

#endif // INCLUDED_OBSTRUCTIONRESOLUTION
