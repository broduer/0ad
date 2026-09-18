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

void UnitCollision::BuildSpatialGrid(const std::vector<MotionStateCompact>& units, int gridSize,
									   Grid<std::vector<size_t>>& spatialGrid, std::vector<std::pair<int, int>>& filledCells,
									   entity_pos_t& gridMinX, entity_pos_t& gridMinZ,
									   entity_pos_t& gridCellSize, int& gridWidth, int& gridHeight)
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

	gridMinX = minX;
	gridMinZ = minZ;
	gridCellSize = entity_pos_t::FromInt(gridSize);

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

void UnitCollision::GetNeighbors(size_t unitIdx, const std::vector<MotionStateCompact>& units,
								   const Grid<std::vector<size_t>>& spatialGrid,
								   const entity_pos_t& gridMinX, const entity_pos_t& gridMinZ,
								   const entity_pos_t& gridCellSize, int gridWidth, int gridHeight,
								   std::vector<size_t>& neighbors)
{
	neighbors.clear();

	const auto& unit = units[unitIdx];
	if (unit.ignore)
		return;

	int cellX = ((unit.position.X - gridMinX) / gridCellSize).ToInt_RoundToZero();
	int cellZ = ((unit.position.Y - gridMinZ) / gridCellSize).ToInt_RoundToZero();

	cellX = std::max(0, std::min(cellX, gridWidth - 1));
	cellZ = std::max(0, std::min(cellZ, gridHeight - 1));

	for (int dx = -1; dx <= 1; ++dx)
	{
		for (int dz = -1; dz <= 1; ++dz)
		{
			int nx = cellX + dx;
			int nz = cellZ + dz;

			if (nx < 0 || nx >= gridWidth || nz < 0 || nz >= gridHeight)
				continue;

			const auto& cellUnits = spatialGrid.get(nx, nz);
			for (size_t neighborIdx : cellUnits)
			{
				if (neighborIdx != unitIdx)
					neighbors.push_back(neighborIdx);
			}
		}
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

	CFixedVector2D offset = a.position - b.position;

	// Most pairs are far apart: reject them before the square root. The (rounded down) length is never shorter than
	// either component, so this only skips pairs that the length test below would reject anyway.
	if (offset.X.Absolute() > maxDist || offset.Y.Absolute() > maxDist)
		return;

	entity_pos_t offsetLength = offset.Length();

	if (offsetLength > maxDist)
		return;

	if (offsetLength <= entity_pos_t::Epsilon() * 10) {
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

	entity_pos_t aWeightInv = entity_pos_t::FromInt(1) / a.weight;
	entity_pos_t bWeightInv = entity_pos_t::FromInt(1) / b.weight;

	outPushA = pushingDir.Multiply(std::min(b.weight.Multiply(timeFactor).Multiply(aWeightInv), maxPushing));
	outPushB = pushingDir.Multiply(-std::min(a.weight.Multiply(timeFactor).Multiply(bWeightInv), maxPushing));

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
	std::vector<uint8_t>& outPressures)
{
	const auto& cfg = PushConfig::GetInstance().GetRuntimeConfig();

	if (!cfg.IsEnabled() || units.empty())
		return;

	outPushes.assign(units.size(), CFixedVector2D());
	outPressures.assign(units.size(), 0);

	// Cells the size of the longest push, so the 3x3 block around a unit still holds every unit that could push it
	// (see ComputePairPush: the widest pair is two units of the largest clearance, moving). Fixed 20 m cells would
	// search a 60x60 m neighbourhood to find pairs at most a few metres apart.
	entity_pos_t maxClearance{entity_pos_t::Zero()};
	for (const MotionStateCompact& unit : units)
		if (!unit.ignore && unit.clearance > maxClearance)
			maxClearance = unit.clearance;

	const entity_pos_t pushRange{(maxClearance * 2).Multiply(PushConfig::PUSHING_CORRECTION).Multiply(cfg.radiusMultiplier) +
		std::max(cfg.movingPushExtension, cfg.staticPushExtension)};
	const int gridSize{std::max(1, pushRange.ToInt_RoundToInfinity())};

	entity_pos_t gridMinX, gridMinZ, gridCellSize;
	int gridWidth, gridHeight;

	BuildSpatialGrid(units, gridSize, spatialGrid, filledCells,
					 gridMinX, gridMinZ, gridCellSize, gridWidth, gridHeight);

	// Reused for every unit, so it's only allocated once per turn.
	std::vector<size_t> neighbors;
	for (size_t i = 0; i < units.size(); ++i)
	{
		if (units[i].ignore)
			continue;

		GetNeighbors(i, units, spatialGrid, gridMinX, gridMinZ, gridCellSize, gridWidth, gridHeight, neighbors);

		for (size_t j : neighbors)
		{
			if (j <= i)
				continue;

			if (units[j].ignore)
				continue;

			CFixedVector2D pushA, pushB;
			uint8_t pressureA, pressureB;

			ComputePairPush(units[i], units[j], dt, cfg, pushA, pushB, pressureA, pressureB);

			outPushes[i] += pushA;
			outPushes[j] += pushB;
			outPressures[i] = std::min<uint8_t>(PushConfig::MAX_PRESSURE,
				outPressures[i] + pressureA);
			outPressures[j] = std::min<uint8_t>(PushConfig::MAX_PRESSURE,
				outPressures[j] + pressureB);
		}
	}
}