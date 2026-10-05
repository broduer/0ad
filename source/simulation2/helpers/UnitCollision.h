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
	 * Compact state structure for efficient batch processing.
	 * Contains only the data needed for push calculations (see ComputePushes).
	 */
	struct MotionStateCompact
	{
		entity_id_t entity;
		CFixedVector2D position;
		// Where the unit would be at the end of the turn if it moved as it did last turn.
		CFixedVector2D predictedPosition;
		entity_pos_t clearance;
		entity_pos_t weight;
		bool isMoving;
		bool ignore;
		entity_id_t controlGroup;
	};

	/**
	 * Compute pushes for all units in one batch.
	 * @param units - compact state of all units
	 * @param dt - timestep
	 * @param spatialGrid, filledCells - kept by the caller between turns, see BuildSpatialGrid.
	 * @param outPushes - output push vectors for each unit
	 * @param outPressures - output pressure increases for each unit
	 */
	static void ComputePushes(
		const std::vector<MotionStateCompact>& units,
		fixed dt,
		Grid<std::vector<size_t>>& spatialGrid,
		std::vector<std::pair<int, int>>& filledCells,
		std::vector<CFixedVector2D>& outPushes,
		std::vector<uint8_t>& outPressures);

private:
	/**
	 * Sort the units into cells of gridSize metres, skipping ignored ones. The grid and filledCells are kept
	 * between turns by the caller: only the cells filled last turn are cleared, and the grid is only resized
	 * when its shape changes. filledCells lists the cells holding at least one unit, in the order they were
	 * first filled.
	 */
	static void BuildSpatialGrid(const std::vector<MotionStateCompact>& units, int gridSize,
								  Grid<std::vector<size_t>>& spatialGrid, std::vector<std::pair<int, int>>& filledCells,
								  int& gridWidth, int& gridHeight);

	static void ComputePairPush(
		const MotionStateCompact& a,
		const MotionStateCompact& b,
		fixed dt,
		const PushConfig::RuntimeConfig& cfg,
		CFixedVector2D& outPushA,
		CFixedVector2D& outPushB,
		uint8_t& outPressureA,
		uint8_t& outPressureB);
};

#endif // INCLUDED_UNITCOLLISION