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

#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
/**
 * The position used for pushing: the midpoint of the unit's current position and its predicted one.
 * ComputePairPush measures the distance between two units from these, and the grid sorts units by them.
 */
CFixedVector2D PushPosition(const UnitCollision::MotionStateCompact& unit)
{
	return (unit.position + unit.predictedPosition) / 2;
}
}

void UnitCollision::BuildSpatialGrid(const std::vector<MotionStateCompact>& units, int gridSize,
									   Grid<std::vector<size_t>>& spatialGrid, std::vector<std::pair<int, int>>& filledCells,
									   int& gridWidth, int& gridHeight)
{
	// Clear only the cells filled last turn: there are at most as many of those as units, while the grid
	// covers the whole area the units are spread over and is mostly empty.
	for (const std::pair<int, int>& cell : filledCells)
		spatialGrid.get(cell.first, cell.second).clear();
	filledCells.clear();

	if (units.empty())
		return;

	const CFixedVector2D first{PushPosition(units[0])};
	entity_pos_t minX = first.X, maxX = first.X;
	entity_pos_t minZ = first.Y, maxZ = first.Y;

	for (const auto& unit : units)
	{
		const CFixedVector2D position{PushPosition(unit)};
		if (position.X < minX) minX = position.X;
		if (position.X > maxX) maxX = position.X;
		if (position.Y < minZ) minZ = position.Y;
		if (position.Y > maxZ) maxZ = position.Y;
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

		const CFixedVector2D position{PushPosition(unit)};
		int cellX = ((position.X - minX) / gridSize).ToInt_RoundToZero();
		int cellZ = ((position.Y - minZ) / gridSize).ToInt_RoundToZero();

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
	uint8_t& outPressureA,
	uint8_t& outPressureB)
{
	outPushA = CFixedVector2D();
	outPushB = CFixedVector2D();
	outPressureA = 0;
	outPressureB = 0;

	int movingPush = (a.isMoving ? 1 : 0) + (b.isMoving ? 1 : 0);
	bool sameControlGroup = (a.controlGroup != INVALID_ENTITY && a.controlGroup == b.controlGroup);

	if (sameControlGroup)
		movingPush = 0;

	if (movingPush == 1)
		return;

	entity_pos_t combinedClearance = (a.clearance + b.clearance).Multiply(PushConfig::PUSHING_CORRECTION);
	entity_pos_t maxDist = combinedClearance;

	if (!sameControlGroup) {
		maxDist = combinedClearance.Multiply(cfg.radiusMultiplier);
		if (movingPush)
			maxDist += cfg.movingPushExtension;
		else
			maxDist += cfg.staticPushExtension;
	}

	// Compare the average of where the two units are and where they are about to be - this makes overall
	// behaviour better, as we really care more about units that end up either crossing paths or staying together.
	CFixedVector2D offset = ((a.position + a.predictedPosition) - (b.position + b.predictedPosition)) / 2;

	// Most pairs are far apart: reject them before the square root. The (rounded down) length is never shorter than
	// either component, so this only skips pairs that the length test below would reject anyway.
	if (offset.X.Absolute() > maxDist || offset.Y.Absolute() > maxDist)
		return;

	entity_pos_t offsetLength = offset.Length();

	if (offsetLength > maxDist)
		return;

	// If the units are about to cross paths, give them a strong perpendicular nudge.
	// Ideally, this will make them look like they avoided each other.
	// Worst case, either the collision detection isn't picked up or they'll end up bogged down.
	// NB: the dot product mostly works because we used average positions earlier.
	// Turn it off for formations - our current 'reforming' code is bad and leads to bad behaviour.
	if (!sameControlGroup && (a.predictedPosition - b.predictedPosition).Dot(a.position - b.position) <
		PushConfig::PERPENDICULAR_NUDGE_THRESHOLD)
	{
		const CFixedVector2D posDelta{(a.predictedPosition - b.predictedPosition) - (a.position - b.position)};
		const CFixedVector2D perp{posDelta.Perpendicular()};
		// Pick the best direction to avoid the target.
		if (offset.Dot(perp) < (-offset).Dot(perp))
			offset = -perp;
		else
			offset = perp;
		offsetLength = offset.Length();
		if (offsetLength > entity_pos_t::Epsilon() * 10)
		{
			// This needs to be a strong effect or it won't really work.
			offset.X = offset.X / offsetLength * 3;
			offset.Y = offset.Y / offsetLength * 3;
		}
		// Counts as overlapping below, for the maximum distance factor.
		offsetLength = entity_pos_t::Zero();
	}
	else if (offsetLength <= entity_pos_t::Epsilon() * 10) {
		bool dir = a.entity % 2;
		offset.X = entity_pos_t::FromInt(dir ? 1 : 0);
		offset.Y = entity_pos_t::FromInt(dir ? 0 : 1);
		offsetLength = entity_pos_t::Epsilon() * 10;
	} else {
		// Normalize
		offset.X = offset.X / offsetLength;
		offset.Y = offset.Y / offsetLength;
	}

	entity_pos_t combinedSpread = maxDist.Multiply(movingPush ? cfg.movingPushingSpread : cfg.staticPushingSpread);
	// The pushing distance factor is 1 at the spread-modified combined clearance, >1 up to MAX if the units 'overlap', < 1 otherwise.
	entity_pos_t distanceFactor = maxDist - combinedSpread;
	// Force units that overlap a lot to have the maximum factor.
	if (distanceFactor <= entity_pos_t::Zero() || offsetLength < combinedSpread / 2) {
		distanceFactor = PushConfig::MAX_DISTANCE_FACTOR;
	} else {
		distanceFactor = std::clamp((maxDist - offsetLength) / distanceFactor,
							entity_pos_t::Zero(),
							PushConfig::MAX_DISTANCE_FACTOR);
	}

	CFixedVector2D pushingDir = offset.Multiply(distanceFactor);

	entity_pos_t timeFactor = dt / PushConfig::PUSHING_REDUCTION_FACTOR;
	entity_pos_t maxPushing = timeFactor * PushConfig::MAX_PUSHING_MULTIPLIER;

	outPushA = pushingDir.Multiply(std::min(b.weight.MulDiv(timeFactor, a.weight), maxPushing));
	outPushB = pushingDir.Multiply(-std::min(a.weight.MulDiv(timeFactor, b.weight), maxPushing));

	int addedPressure = std::max(0, (PushConfig::PRESSURE_STATIC_FACTOR +
		(distanceFactor + entity_pos_t::FromInt(-2) / 3) * PushConfig::PRESSURE_DISTANCE_FACTOR)
		.Multiply(cfg.pressureStrength).ToInt_RoundToZero());

	outPressureA = static_cast<uint8_t>(std::min(PushConfig::MAX_PRESSURE, addedPressure));
	outPressureB = outPressureA;
}

void UnitCollision::ComputePushes(
	const std::vector<MotionStateCompact>& units,
	fixed dt,
	Grid<std::vector<size_t>>& spatialGrid,
	std::vector<std::pair<int, int>>& filledCells,
	std::vector<CFixedVector2D>& outPushes,
	std::vector<uint8_t>& outPressures)
{
	const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();

	if (!cfg.IsEnabled() || units.empty())
		return;

	outPushes.assign(units.size(), CFixedVector2D());
	outPressures.assign(units.size(), 0);

	// Units are sorted into square cells, and each unit is compared with those in its own cell and the eight
	// around it. To find every pair that can push each other, a cell must be at least as wide as the longest
	// push there can be this turn: that between two moving units of the largest clearance present.
	entity_pos_t maxClearance{entity_pos_t::Zero()};
	for (const MotionStateCompact& unit : units)
		if (!unit.ignore && unit.clearance > maxClearance)
			maxClearance = unit.clearance;

	const entity_pos_t pushRange{(maxClearance * 2).Multiply(PushConfig::PUSHING_CORRECTION).Multiply(cfg.radiusMultiplier) +
		std::max(cfg.movingPushExtension, cfg.staticPushExtension)};
	const int gridSize{std::max(1, pushRange.ToInt_RoundToInfinity())};

	int gridWidth, gridHeight;
	BuildSpatialGrid(units, gridSize, spatialGrid, filledCells, gridWidth, gridHeight);

	// Always compute a pair with the lower index first. The sweep below meets two units in an order that
	// depends on which side of a cell line each is on, and with fixed-point rounding, swapping them can
	// change the push in its last digit.
	const auto accumulate = [&](const size_t first, const size_t second)
	{
		const size_t i{std::min(first, second)};
		const size_t j{std::max(first, second)};
		CFixedVector2D pushA, pushB;
		uint8_t pressureA, pressureB;

		ComputePairPush(units[i], units[j], dt, cfg, pushA, pushB, pressureA, pressureB);

		outPushes[i] += pushA;
		outPushes[j] += pushB;
		outPressures[i] = static_cast<uint8_t>(std::min<int>(PushConfig::MAX_PRESSURE, outPressures[i] + pressureA));
		outPressures[j] = static_cast<uint8_t>(std::min<int>(PushConfig::MAX_PRESSURE, outPressures[j] + pressureB));
	};

	// Visit every pair of units in the same or neighbouring cells exactly once: for each filled cell C,
	// the pairs inside it, then the pairs with the four neighbours marked o. The four marked . are paired
	// with C when the loop gets to them: C is then one of their o cells. Ignored units are not in the grid.
	//
	//   z+1   .   o   o
	//   z     .   C   o
	//   z-1   .   .   o
	//        x-1  x  x+1
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
}
