/* Copyright (C) 2025 Wildfire Games.
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

#include "LongPathfinder.h"

#include "graphics/SColor.h"
#include "lib/bits.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "ps/CLogger.h"
#include "ps/Profiler2.h"
#include "renderer/TerrainOverlay.h"
#include "simulation2/helpers/HierarchicalPathfinder.h"
#include "simulation2/helpers/Pathfinding.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <utility>

namespace
{
static std::mutex g_DebugMutex;
constexpr u8 JPC_CHUNK_SIZE = 48;
constexpr u8 JPC_CONTINUE = 0xFF;
constexpr u8 JPC_OBSTRUCTION_BIT = 0x80;
constexpr u8 JPC_COORD_MASK = 0x7F;
static_assert(JPC_CHUNK_SIZE < JPC_COORD_MASK);
constexpr u16 MAX_SHIPPED_MAP_TILES = 512;
static_assert(
	(MAX_SHIPPED_MAP_TILES * Pathfinding::NAVCELLS_PER_TERRAIN_TILE + JPC_CHUNK_SIZE - 1) / JPC_CHUNK_SIZE <= std::numeric_limits<u8>::max(),
	"DirtyStripeMask chunk indices assume supported map sizes fit in u8.");

size_t PassClassToIndex(pass_class_t passClass)
{
	ENSURE(passClass);
	ENSURE((passClass & (passClass - 1)) == 0);
	return std::countr_zero(passClass);
}

struct DirtyStripeMask
{
	u8 m_ChunksW = 0;
	u8 m_ChunksH = 0;
	std::vector<u8> m_HorizontalRows;
	std::vector<u8> m_VerticalColumns;

	bool IsHorizontalDirty(u8 ci, u8 cj, u8 localRow) const
	{
		return m_HorizontalRows[(cj * m_ChunksW + ci) * JPC_CHUNK_SIZE + localRow] != 0;
	}

	bool IsVerticalDirty(u8 ci, u8 cj, u8 localColumn) const
	{
		return m_VerticalColumns[(cj * m_ChunksW + ci) * JPC_CHUNK_SIZE + localColumn] != 0;
	}

	bool HasAnyDirtyStripe(u8 ci, u8 cj) const
	{
		const size_t base = (cj * m_ChunksW + ci) * JPC_CHUNK_SIZE;
		for (u8 local = 0; local < JPC_CHUNK_SIZE; ++local)
			if (m_HorizontalRows[base + local] != 0 || m_VerticalColumns[base + local] != 0)
				return true;
		return false;
	}
};

void MarkHorizontalDirtyRow(DirtyStripeMask& dirty, u16 gridHeight, u8 ci, int globalRow)
{
	if (globalRow < 0 || globalRow >= gridHeight)
		return;

	const u8 cj = globalRow / JPC_CHUNK_SIZE;
	const u8 localRow = globalRow % JPC_CHUNK_SIZE;
	dirty.m_HorizontalRows[(cj * dirty.m_ChunksW + ci) * JPC_CHUNK_SIZE + localRow] = 1;
}

void MarkVerticalDirtyColumn(DirtyStripeMask& dirty, u16 gridWidth, int globalColumn, u8 cj)
{
	if (globalColumn < 0 || globalColumn >= gridWidth)
		return;

	const u8 ci = globalColumn / JPC_CHUNK_SIZE;
	const u8 localColumn = globalColumn % JPC_CHUNK_SIZE;
	dirty.m_VerticalColumns[(cj * dirty.m_ChunksW + ci) * JPC_CHUNK_SIZE + localColumn] = 1;
}

DirtyStripeMask ComputeDirtyStripes(const Grid<u8>& dirtinessGrid, u16 gridWidth, u16 gridHeight)
{
	DirtyStripeMask dirty;
	dirty.m_ChunksW = (gridWidth + JPC_CHUNK_SIZE - 1) / JPC_CHUNK_SIZE;
	dirty.m_ChunksH = (gridHeight + JPC_CHUNK_SIZE - 1) / JPC_CHUNK_SIZE;
	dirty.m_HorizontalRows.assign(dirty.m_ChunksW * dirty.m_ChunksH * JPC_CHUNK_SIZE, 0);
	dirty.m_VerticalColumns.assign(dirty.m_ChunksW * dirty.m_ChunksH * JPC_CHUNK_SIZE, 0);

	if (dirtinessGrid.m_W == 0 || dirtinessGrid.m_H == 0)
		return dirty;

	for (u16 j = 0; j < dirtinessGrid.m_H; ++j)
	{
		for (u16 i = 0; i < dirtinessGrid.m_W; ++i)
		{
			if (!dirtinessGrid.get(i, j))
				continue;

			const u8 ci = i / JPC_CHUNK_SIZE;
			const u8 cj = j / JPC_CHUNK_SIZE;
			const int chunkStartI = ci * JPC_CHUNK_SIZE;
			const int chunkStartJ = cj * JPC_CHUNK_SIZE;
			const int chunkEndI = std::min<int>(chunkStartI + JPC_CHUNK_SIZE, gridWidth) - 1;
			const int chunkEndJ = std::min<int>(chunkStartJ + JPC_CHUNK_SIZE, gridHeight) - 1;

			for (int row = j - 1; row <= j + 1; ++row)
			{
				MarkHorizontalDirtyRow(dirty, gridHeight, ci, row);
				if (i == chunkStartI && ci > 0)
					MarkHorizontalDirtyRow(dirty, gridHeight, ci - 1, row);
				if (i == chunkEndI && ci + 1 < dirty.m_ChunksW)
					MarkHorizontalDirtyRow(dirty, gridHeight, ci + 1, row);
			}

			for (int column = i - 1; column <= i + 1; ++column)
			{
				MarkVerticalDirtyColumn(dirty, gridWidth, column, cj);
				if (j == chunkStartJ && cj > 0)
					MarkVerticalDirtyColumn(dirty, gridWidth, column, cj - 1);
				if (j == chunkEndJ && cj + 1 < dirty.m_ChunksH)
					MarkVerticalDirtyColumn(dirty, gridWidth, column, cj + 1);
			}
		}
	}

	return dirty;
}
}

/**
 * Jump point cache.
 *
 * The JPS algorithm wants to efficiently either find the first jump point
 * in some direction from some cell (not counting the cell itself),
 * if it is reachable without crossing any impassable cells;
 * or know that there is no such reachable jump point.
 * The jump point is always on a passable cell.
 * We cache that data to allow fast lookups, which helps performance
 * significantly (especially on sparse maps).
 * Recalculation might be expensive but the underlying passability data
 * changes relatively rarely.
 *
 * To allow the algorithm to detect goal cells, we want to treat them as
 * jump points too. (That means the algorithm will push those cells onto
 * its open queue, and will eventually pop a goal cell and realise it's done.)
 * (Goals might be circles/squares/etc, not just a single cell.)
 * But the goal generally changes for every path request, so we can't cache
 * it like the normal jump points.
 * Instead, if there's no jump point from some cell then we'll cache the
 * first impassable cell as an 'obstruction jump point'
 * (with a flag to distinguish from a real jump point), and then the caller
 * can test whether the goal includes a cell that's closer than the first
 * (obstruction or real) jump point,
 * and treat the goal cell as a jump point in that case.
 *
 * We only ever need to find the jump point relative to a passable cell;
 * the cache is allowed to return bogus values for impassable cells.
 */
class JumpPointCache
{
public:
	void Reload(const Grid<NavcellData>& terrain, pass_class_t passClass)
	{
		PROFILE2("JumpPointCache rebuild all");

		m_Width = terrain.m_W;
		m_Height = terrain.m_H;
		m_ChunksW = (terrain.m_W + JPC_CHUNK_SIZE - 1) / JPC_CHUNK_SIZE;
		m_ChunksH = (terrain.m_H + JPC_CHUNK_SIZE - 1) / JPC_CHUNK_SIZE;
		m_Data.assign(TotalDataSize(), 0);

		for (u8 cj = 0; cj < m_ChunksH; ++cj)
			for (u8 ci = 0; ci < m_ChunksW; ++ci)
				BuildChunk(terrain, passClass, ci, cj);
	}

	void RebuildDirty(const Grid<NavcellData>& terrain, pass_class_t passClass, const DirtyStripeMask& dirty)
	{
		PROFILE2("JumpPointCache rebuild dirty stripes");

		if (m_Width != terrain.m_W || m_Height != terrain.m_H)
		{
			Reload(terrain, passClass);
			return;
		}

		ENSURE(dirty.m_ChunksW == m_ChunksW);
		ENSURE(dirty.m_ChunksH == m_ChunksH);

		for (u8 cj = 0; cj < m_ChunksH; ++cj)
			for (u8 ci = 0; ci < m_ChunksW; ++ci)
				if (dirty.HasAnyDirtyStripe(ci, cj))
					BuildChunk(terrain, passClass, ci, cj, &dirty);
	}

	size_t GetMemoryUsage() const
	{
		return m_Data.capacity() * sizeof(u8);
	}

	int GetJumpPointRight(int i, int j, const PathGoal& goal) const
	{
		return GetJumpPoint<false, true>(i, j, goal);
	}

	int GetJumpPointLeft(int i, int j, const PathGoal& goal) const
	{
		return GetJumpPoint<false, false>(i, j, goal);
	}

	int GetJumpPointUp(int i, int j, const PathGoal& goal) const
	{
		return GetJumpPoint<true, true>(i, j, goal);
	}

	int GetJumpPointDown(int i, int j, const PathGoal& goal) const
	{
		return GetJumpPoint<true, false>(i, j, goal);
	}

private:
	template<bool VERTICAL, bool POSITIVE>
	int GetJumpPoint(int i, int j, const PathGoal& goal) const
	{
		const int sourceI = i;
		const int sourceJ = j;
		const int step = POSITIVE ? 1 : -1;
		int chunkI = i / JPC_CHUNK_SIZE;
		int chunkJ = j / JPC_CHUNK_SIZE;
		int sourceSlot = (VERTICAL ? j : i) % JPC_CHUNK_SIZE;

		while (true)
		{
			const int localFixed = VERTICAL ? i - chunkI * JPC_CHUNK_SIZE : j - chunkJ * JPC_CHUNK_SIZE;
			const int chunkStart = (VERTICAL ? chunkJ : chunkI) * JPC_CHUNK_SIZE;

			// For NEGATIVE at sourceSlot==0, the first candidate is before the chunk start,
			// so treat this as a virtual CONTINUE to the previous chunk.
			const u8 entry = (!POSITIVE && sourceSlot == 0) ? JPC_CONTINUE :
				GetEntry<VERTICAL, POSITIVE>(chunkI, chunkJ, localFixed, sourceSlot);
			const int boundary = POSITIVE ? chunkStart + PrimaryLength<VERTICAL>(chunkI, chunkJ) - 1 : chunkStart;

			if (entry == JPC_CONTINUE)
			{
				int goalCoord;
				if constexpr (VERTICAL)
				{
					if ((POSITIVE && j + 1 <= boundary && goal.NavcellRectContainsGoal(i, j + 1, i, boundary, NULL, &goalCoord)) ||
						(!POSITIVE && j - 1 >= boundary && goal.NavcellRectContainsGoal(i, j - 1, i, boundary, NULL, &goalCoord)))
						return goalCoord;

					if ((POSITIVE && chunkJ + 1 >= m_ChunksH) ||
						(!POSITIVE && chunkJ == 0))
						return sourceJ;

					chunkJ += step;
					sourceSlot = POSITIVE ? -1 : PrimaryLength<VERTICAL>(chunkI, chunkJ);
					j = boundary;
				}
				else
				{
					if ((POSITIVE && i + 1 <= boundary && goal.NavcellRectContainsGoal(i + 1, j, boundary, j, &goalCoord, NULL)) ||
						(!POSITIVE && i - 1 >= boundary && goal.NavcellRectContainsGoal(i - 1, j, boundary, j, &goalCoord, NULL)))
						return goalCoord;

					if ((POSITIVE && chunkI + 1 >= m_ChunksW) ||
						(!POSITIVE && chunkI == 0))
						return sourceI;

					chunkI += step;
					sourceSlot = POSITIVE ? -1 : PrimaryLength<VERTICAL>(chunkI, chunkJ);
					i = boundary;
				}
				continue;
			}

			const int jump = chunkStart + (entry & JPC_COORD_MASK);
			int goalCoord;
			if constexpr (VERTICAL)
			{
				if ((POSITIVE && j + 1 <= jump - 1 && goal.NavcellRectContainsGoal(i, j + 1, i, jump - 1, NULL, &goalCoord)) ||
					(!POSITIVE && j - 1 >= jump + 1 && goal.NavcellRectContainsGoal(i, j - 1, i, jump + 1, NULL, &goalCoord)))
					return goalCoord;
			}
			else
			{
				if ((POSITIVE && i + 1 <= jump - 1 && goal.NavcellRectContainsGoal(i + 1, j, jump - 1, j, &goalCoord, NULL)) ||
					(!POSITIVE && i - 1 >= jump + 1 && goal.NavcellRectContainsGoal(i - 1, j, jump + 1, j, &goalCoord, NULL)))
					return goalCoord;
			}

			if ((entry & JPC_OBSTRUCTION_BIT) == 0)
				return jump;
			return VERTICAL ? sourceJ : sourceI;
		}
	}

	static bool IsPassable(const Grid<NavcellData>& terrain, pass_class_t passClass, int i, int j)
	{
		if (i < 0 || j < 0 || i >= terrain.m_W || j >= terrain.m_H)
			return false;
		return IS_PASSABLE(terrain.get(i, j), passClass);
	}

	template<bool VERTICAL, bool POSITIVE>
	static bool IsJumpPoint(const Grid<NavcellData>& terrain, pass_class_t passClass, int i, int j)
	{
		if constexpr (!VERTICAL && POSITIVE)
			return (!IsPassable(terrain, passClass, i - 1, j - 1) && IsPassable(terrain, passClass, i, j - 1)) ||
				(!IsPassable(terrain, passClass, i - 1, j + 1) && IsPassable(terrain, passClass, i, j + 1));
		if constexpr (!VERTICAL && !POSITIVE)
			return (!IsPassable(terrain, passClass, i + 1, j - 1) && IsPassable(terrain, passClass, i, j - 1)) ||
				(!IsPassable(terrain, passClass, i + 1, j + 1) && IsPassable(terrain, passClass, i, j + 1));
		if constexpr (VERTICAL && POSITIVE)
			return (!IsPassable(terrain, passClass, i - 1, j - 1) && IsPassable(terrain, passClass, i - 1, j)) ||
				(!IsPassable(terrain, passClass, i + 1, j - 1) && IsPassable(terrain, passClass, i + 1, j));
		return (!IsPassable(terrain, passClass, i - 1, j + 1) && IsPassable(terrain, passClass, i - 1, j)) ||
			(!IsPassable(terrain, passClass, i + 1, j + 1) && IsPassable(terrain, passClass, i + 1, j));
	}

	static u8 Encode(int coord, bool obstruction)
	{
		return static_cast<u8>(coord | (obstruction ? JPC_OBSTRUCTION_BIT : 0));
	}

	int ChunkWidth(u8 ci) const
	{
		return std::min<int>(JPC_CHUNK_SIZE, m_Width - ci * JPC_CHUNK_SIZE);
	}

	int ChunkHeight(u8 cj) const
	{
		return std::min<int>(JPC_CHUNK_SIZE, m_Height - cj * JPC_CHUNK_SIZE);
	}

	template<bool VERTICAL>
	int PrimaryLength(u8 ci, u8 cj) const
	{
		if constexpr (VERTICAL)
			return ChunkHeight(cj);
		return ChunkWidth(ci);
	}

	template<bool VERTICAL>
	int FixedLength(u8 ci, u8 cj) const
	{
		if constexpr (VERTICAL)
			return ChunkWidth(ci);
		return ChunkHeight(cj);
	}

	static size_t ChunkSize(int width, int height)
	{
		return 2 * height * (width + 1) + 2 * width * (height + 1);
	}

	size_t RowSize(int height) const
	{
		return (m_ChunksW - 1) * ChunkSize(JPC_CHUNK_SIZE, height) +
			ChunkSize(ChunkWidth(m_ChunksW - 1), height);
	}

	size_t TotalDataSize() const
	{
		return (m_ChunksH - 1) * RowSize(JPC_CHUNK_SIZE) +
			RowSize(ChunkHeight(m_ChunksH - 1));
	}

	size_t ChunkOffset(u8 ci, u8 cj) const
	{
		return cj * RowSize(JPC_CHUNK_SIZE) + ci * ChunkSize(JPC_CHUNK_SIZE, ChunkHeight(cj));
	}

	template<bool VERTICAL, bool POSITIVE>
	size_t DirectionOffset(u8 ci, u8 cj) const
	{
		const int width = ChunkWidth(ci);
		const int height = ChunkHeight(cj);
		size_t offset = ChunkOffset(ci, cj);
		if constexpr (!VERTICAL && POSITIVE)
			return offset;
		offset += height * (width + 1);
		if constexpr (!VERTICAL && !POSITIVE)
			return offset;
		offset += height * (width + 1);
		if constexpr (VERTICAL && POSITIVE)
			return offset;
		return offset + width * (height + 1);
	}

	template<bool VERTICAL, bool POSITIVE>
	size_t EntryIndex(u8 ci, u8 cj, int localFixed, int sourceSlot) const
	{
		const int stride = PrimaryLength<VERTICAL>(ci, cj) + 1;
		if constexpr (POSITIVE)
			return DirectionOffset<VERTICAL, POSITIVE>(ci, cj) + localFixed * stride + sourceSlot + 1;
		return DirectionOffset<VERTICAL, POSITIVE>(ci, cj) + localFixed * stride + sourceSlot;
	}

	template<bool VERTICAL, bool POSITIVE>
	u8& GetEntry(u8 ci, u8 cj, int localFixed, int sourceSlot)
	{
		return m_Data[EntryIndex<VERTICAL, POSITIVE>(ci, cj, localFixed, sourceSlot)];
	}

	template<bool VERTICAL, bool POSITIVE>
	u8 GetEntry(u8 ci, u8 cj, int localFixed, int sourceSlot) const
	{
		return m_Data[EntryIndex<VERTICAL, POSITIVE>(ci, cj, localFixed, sourceSlot)];
	}

	template<bool VERTICAL, bool POSITIVE>
	void BuildDirection(u8 ci, u8 cj, const Grid<NavcellData>& terrain, pass_class_t passClass, int i0, int j0, const DirtyStripeMask* dirty)
	{
		const int primaryLength = PrimaryLength<VERTICAL>(ci, cj);
		const int fixedLength = FixedLength<VERTICAL>(ci, cj);
		const int stride = primaryLength + 1;
		const bool hasNextChunk = POSITIVE ? (VERTICAL ? cj + 1 < m_ChunksH : ci + 1 < m_ChunksW) :
			(VERTICAL ? cj > 0 : ci > 0);
		u8* const directionData = m_Data.data() + DirectionOffset<VERTICAL, POSITIVE>(ci, cj);

		for (int localFixed = 0; localFixed < fixedLength; ++localFixed)
		{
			if (dirty)
			{
				if constexpr (VERTICAL)
				{
					if (!dirty->IsVerticalDirty(ci, cj, localFixed))
						continue;
				}
				else
				{
					if (!dirty->IsHorizontalDirty(ci, cj, localFixed))
						continue;
				}
			}

			u8* const row = directionData + localFixed * stride;

			if constexpr (POSITIVE)
			{
				u8 carry = hasNextChunk ? JPC_CONTINUE : Encode(primaryLength, true);
				for (int source = primaryLength - 1; source >= -1; --source)
				{
					const int candidate = source + 1;
					const int globalI = i0 + (VERTICAL ? localFixed : candidate);
					const int globalJ = j0 + (VERTICAL ? candidate : localFixed);
					const int coord = VERTICAL ? globalJ - j0 : globalI - i0;
					if (!IsPassable(terrain, passClass, globalI, globalJ))
						carry = Encode(coord, true);
					else if (IsJumpPoint<VERTICAL, POSITIVE>(terrain, passClass, globalI, globalJ))
						carry = Encode(coord, false);
					row[source + 1] = carry;
				}
			}
			else
			{
				u8 carry = hasNextChunk ? JPC_CONTINUE : Encode(0, true);
				for (int source = 1; source <= primaryLength; ++source)
				{
					const int candidate = source - 1;
					const int globalI = i0 + (VERTICAL ? localFixed : candidate);
					const int globalJ = j0 + (VERTICAL ? candidate : localFixed);
					const int coord = VERTICAL ? globalJ - j0 : globalI - i0;
					if (!IsPassable(terrain, passClass, globalI, globalJ))
						carry = Encode(coord, true);
					else if (IsJumpPoint<VERTICAL, POSITIVE>(terrain, passClass, globalI, globalJ))
						carry = Encode(coord, false);
					row[source] = carry;
				}
			}
		}
	}

	void BuildChunk(const Grid<NavcellData>& terrain, pass_class_t passClass, u8 ci, u8 cj, const DirtyStripeMask* dirty = nullptr)
	{
		const int i0 = ci * JPC_CHUNK_SIZE;
		const int j0 = cj * JPC_CHUNK_SIZE;

		BuildDirection<false, true>(ci, cj, terrain, passClass, i0, j0, dirty);
		BuildDirection<false, false>(ci, cj, terrain, passClass, i0, j0, dirty);
		BuildDirection<true, true>(ci, cj, terrain, passClass, i0, j0, dirty);
		BuildDirection<true, false>(ci, cj, terrain, passClass, i0, j0, dirty);
	}

	u16 m_Width = 0;
	u16 m_Height = 0;
	u8 m_ChunksW = 0;
	u8 m_ChunksH = 0;
	std::vector<u8> m_Data;
};

//////////////////////////////////////////////////////////

LongPathfinder::LongPathfinder() :
	m_UseJPSCache(true),
	m_Grid(NULL), m_GridSize(0)
{
}

void LongPathfinder::Reload(Grid<NavcellData>* passabilityGrid, const std::map<std::string, pass_class_t>& pathfindingPassClasses)
{
	m_Grid = passabilityGrid;
	ASSERT(passabilityGrid->m_H == passabilityGrid->m_W);
	m_GridSize = passabilityGrid->m_W;

	for (std::unique_ptr<JumpPointCache>& cache : m_JumpPointCache)
		cache.reset();

	if (!m_UseJPSCache)
		return;

	for (const std::pair<const std::string, pass_class_t>& passClassMask : pathfindingPassClasses)
	{
		const size_t idx = PassClassToIndex(passClassMask.second);
		m_JumpPointCache[idx] = std::make_unique<JumpPointCache>();
		m_JumpPointCache[idx]->Reload(*m_Grid, passClassMask.second);
	}
}

void LongPathfinder::Update(Grid<NavcellData>* passabilityGrid, const Grid<u8>& dirtinessGrid, const std::map<std::string, pass_class_t>& pathfindingPassClasses)
{
	m_Grid = passabilityGrid;
	ASSERT(passabilityGrid->m_H == passabilityGrid->m_W);
	ASSERT(m_GridSize == passabilityGrid->m_H);

	if (!m_UseJPSCache)
		return;
	const DirtyStripeMask dirty = ComputeDirtyStripes(dirtinessGrid, passabilityGrid->m_W, passabilityGrid->m_H);
	std::array<bool, PASS_CLASS_BITS> activePassClasses = {};

	for (const std::pair<const std::string, pass_class_t>& passClassMask : pathfindingPassClasses)
	{
		const size_t idx = PassClassToIndex(passClassMask.second);
		activePassClasses[idx] = true;
		if (!m_JumpPointCache[idx])
			m_JumpPointCache[idx] = std::make_unique<JumpPointCache>();
		m_JumpPointCache[idx]->RebuildDirty(*m_Grid, passClassMask.second, dirty);
	}

	for (size_t i = 0; i < m_JumpPointCache.size(); ++i)
		if (!activePassClasses[i])
			m_JumpPointCache[i].reset();
}

#define PASSABLE(i, j) IS_PASSABLE(state.terrain->get(i, j), state.passClass)

// Calculate heuristic cost from tile i,j to goal
// (This ought to be an underestimate for correctness)
PathCost LongPathfinder::CalculateHeuristic(int i, int j, int iGoal, int jGoal) const
{
	int di = abs(i - iGoal);
	int dj = abs(j - jGoal);
	int diag = std::min(di, dj);
	return PathCost(di - diag + dj - diag, diag);
}

// Do the A* processing for a neighbour tile i,j.
void LongPathfinder::ProcessNeighbour(int pi, int pj, int i, int j, PathCost pg, PathfinderState& state) const
{
	// Reject impassable tiles
	if (!PASSABLE(i, j))
		return;

	PathfindTile& n = state.tiles->get(i, j);

	if (n.IsClosed())
		return;

	PathCost dg;
	if (pi == i)
		dg = PathCost::horizvert(abs(pj - j));
	else if (pj == j)
		dg = PathCost::horizvert(abs(pi - i));
	else
	{
		ASSERT(abs((int)pi - (int)i) == abs((int)pj - (int)j)); // must be 45 degrees
		dg = PathCost::diag(abs((int)pi - (int)i));
	}

	PathCost g = pg + dg; // cost to this tile = cost to predecessor + delta from predecessor

	PathCost h = CalculateHeuristic(i, j, state.iGoal, state.jGoal);

	// If this is a new tile, compute the heuristic distance
	if (n.IsUnexplored())
	{
		// Remember the best tile we've seen so far, in case we never actually reach the target
		if (h < state.hBest)
		{
			state.hBest = h;
			state.iBest = i;
			state.jBest = j;
		}
	}
	else
	{
		// If we've already seen this tile, and the new path to this tile does not have a
		// better cost, then stop now
		if (g >= n.GetCost())
			return;

		// Otherwise, we have a better path.

		// If we've already added this tile to the open list:
		if (n.IsOpen())
		{
			// This is a better path, so replace the old one with the new cost/parent
			PathCost gprev = n.GetCost();
			n.SetCost(g);
			n.SetPred(pi, pj, i, j);
			state.open.promote(TileID(i, j), gprev + h, g + h, h);
			return;
		}
	}

	// Add it to the open list:
	n.SetStatusOpen();
	n.SetCost(g);
	n.SetPred(pi, pj, i, j);
	PriorityQueue::Item t = { TileID(i, j), g + h, h };
	state.open.push(t);
}

/*
 * In the JPS algorithm, after a tile is taken off the open queue,
 * we don't process every adjacent neighbour (as in standard A*).
 * Instead we only move in a subset of directions (depending on the
 * direction from the predecessor); and instead of moving by a single
 * cell, we move up to the next jump point in that direction.
 * The AddJumped... functions do this by calling ProcessNeighbour
 * on the jump point (if any) in a certain direction.
 * The HasJumped... functions return whether there is any jump point
 * in that direction.
 */

// JPS functions scan navcells towards one direction
// OnTheWay tests whether we are scanning towards the right direction, to avoid useless scans
inline bool OnTheWay(int i, int j, int di, int dj, int iGoal, int jGoal)
{
	if (dj != 0)
	{
		// We're not moving towards the goal
		if ((jGoal - j) * dj < 0)
			return false;
	}
	else if (j != jGoal)
		return false;

	if (di != 0)
	{
		// We're not moving towards the goal
		if ((iGoal - i) * di < 0)
			return false;
	}
	else if (i != iGoal)
		return false;

	return true;
}


void LongPathfinder::AddJumpedHoriz(int i, int j, int di, PathCost g, PathfinderState& state, bool detectGoal) const
{
	if (state.jpc)
	{
		int jump;
		if (di > 0)
			jump = state.jpc->GetJumpPointRight(i, j, state.goal);
		else
			jump = state.jpc->GetJumpPointLeft(i, j, state.goal);

		if (jump != i)
		{
			if (detectGoal && state.goal.NavcellContainsGoal(jump, j))
				state.open.clear();
			ProcessNeighbour(i, j, jump, j, g, state);
		}
	}
	else
	{
		ASSERT(di == 1 || di == -1);
		int ni = i + di;
		while (true)
		{
			if (!PASSABLE(ni, j))
				break;

			if (detectGoal && state.goal.NavcellContainsGoal(ni, j))
			{
				state.open.clear();
				ProcessNeighbour(i, j, ni, j, g, state);
				break;
			}

			if	((!PASSABLE(ni - di, j - 1) && PASSABLE(ni, j - 1)) ||
				(!PASSABLE(ni - di, j + 1) && PASSABLE(ni, j + 1)))
			{
				ProcessNeighbour(i, j, ni, j, g, state);
				break;
			}

			ni += di;
		}
	}
}

// Returns the i-coordinate of the jump point if it exists, else returns i
int LongPathfinder::HasJumpedHoriz(int i, int j, int di, PathfinderState& state, bool detectGoal) const
{
	if (state.jpc)
	{
		int jump;
		if (di > 0)
			jump = state.jpc->GetJumpPointRight(i, j, state.goal);
		else
			jump = state.jpc->GetJumpPointLeft(i, j, state.goal);

		if (detectGoal && jump != i && state.goal.NavcellContainsGoal(jump, j))
			state.open.clear();

		return jump;
	}
	else
	{
		ASSERT(di == 1 || di == -1);
		int ni = i + di;
		while (true)
		{
			if (!PASSABLE(ni, j))
				return i;

			if (detectGoal && state.goal.NavcellContainsGoal(ni, j))
			{
				state.open.clear();
				return ni;
			}

			if	((!PASSABLE(ni - di, j - 1) && PASSABLE(ni, j - 1)) ||
				(!PASSABLE(ni - di, j + 1) && PASSABLE(ni, j + 1)))
				return ni;

			ni += di;
		}
	}
}

void LongPathfinder::AddJumpedVert(int i, int j, int dj, PathCost g, PathfinderState& state, bool detectGoal) const
{
	if (state.jpc)
	{
		int jump;
		if (dj > 0)
			jump = state.jpc->GetJumpPointUp(i, j, state.goal);
		else
			jump = state.jpc->GetJumpPointDown(i, j, state.goal);

		if (jump != j)
		{
			if (detectGoal && state.goal.NavcellContainsGoal(i, jump))
				state.open.clear();
			ProcessNeighbour(i, j, i, jump, g, state);
		}
	}
	else
	{
		ASSERT(dj == 1 || dj == -1);
		int nj = j + dj;
		while (true)
		{
			if (!PASSABLE(i, nj))
				break;

			if (detectGoal && state.goal.NavcellContainsGoal(i, nj))
			{
				state.open.clear();
				ProcessNeighbour(i, j, i, nj, g, state);
				break;
			}

			if	((!PASSABLE(i - 1, nj - dj) && PASSABLE(i - 1, nj)) ||
				(!PASSABLE(i + 1, nj - dj) && PASSABLE(i + 1, nj)))
			{
				ProcessNeighbour(i, j, i, nj, g, state);
				break;
			}

			nj += dj;
		}
	}
}

// Returns the j-coordinate of the jump point if it exists, else returns j
int LongPathfinder::HasJumpedVert(int i, int j, int dj, PathfinderState& state, bool detectGoal) const
{
	if (state.jpc)
	{
		int jump;
		if (dj > 0)
			jump = state.jpc->GetJumpPointUp(i, j, state.goal);
		else
			jump = state.jpc->GetJumpPointDown(i, j, state.goal);

		if (detectGoal && jump != j && state.goal.NavcellContainsGoal(i, jump))
			state.open.clear();

		return jump;
	}
	else
	{
		ASSERT(dj == 1 || dj == -1);
		int nj = j + dj;
		while (true)
		{
			if (!PASSABLE(i, nj))
				return j;

			if (detectGoal && state.goal.NavcellContainsGoal(i, nj))
			{
				state.open.clear();
				return nj;
			}

			if	((!PASSABLE(i - 1, nj - dj) && PASSABLE(i - 1, nj)) ||
				(!PASSABLE(i + 1, nj - dj) && PASSABLE(i + 1, nj)))
				return nj;

			nj += dj;
		}
	}
}

/*
 * We never cache diagonal jump points - they're usually so frequent that
 * a linear search is about as cheap and avoids the setup cost and memory cost.
 */
void LongPathfinder::AddJumpedDiag(int i, int j, int di, int dj, PathCost g, PathfinderState& state) const
{
	// 	ProcessNeighbour(i, j, i + di, j + dj, g, state);
	// 	return;

	ASSERT(di == 1 || di == -1);
	ASSERT(dj == 1 || dj == -1);

	int ni = i + di;
	int nj = j + dj;
	bool detectGoal = OnTheWay(i, j, di, dj, state.iGoal, state.jGoal);
	while (true)
	{
		// Stop if we hit an obstructed cell
		if (!PASSABLE(ni, nj))
			return;

		// Stop if moving onto this cell caused us to
		// touch the corner of an obstructed cell
		if (!PASSABLE(ni - di, nj) || !PASSABLE(ni, nj - dj))
			return;

		// Process this cell if it's at the goal
		if (detectGoal && state.goal.NavcellContainsGoal(ni, nj))
		{
			state.open.clear();
			ProcessNeighbour(i, j, ni, nj, g, state);
			return;
		}

		int fi = HasJumpedHoriz(ni, nj, di, state, detectGoal && OnTheWay(ni, nj, di, 0, state.iGoal, state.jGoal));
		int fj = HasJumpedVert(ni, nj, dj, state, detectGoal && OnTheWay(ni, nj, 0, dj, state.iGoal, state.jGoal));
		if (fi != ni || fj != nj)
		{
			ProcessNeighbour(i, j, ni, nj, g, state);
			g += PathCost::diag(abs(ni - i));

			if (fi != ni)
				ProcessNeighbour(ni, nj, fi, nj, g, state);

			if (fj != nj)
				ProcessNeighbour(ni, nj, ni, fj, g, state);

			return;
		}

		ni += di;
		nj += dj;
	}
}

void LongPathfinder::ComputeJPSPath(const HierarchicalPathfinder& hierPath, entity_pos_t x0, entity_pos_t z0, const PathGoal& origGoal, pass_class_t passClass, WaypointPath& path) const
{
	PROFILE2("ComputePathJPS");
	PathfinderState state = { 0 };

	if (m_UseJPSCache && passClass != SPECIAL_PASS_CLASS)
	{
		const size_t idx = PassClassToIndex(passClass);
		state.jpc = m_JumpPointCache[idx].get();
	}

	// Convert the start coordinates to tile indexes
	u16 i0, j0;
	Pathfinding::NearestNavcell(x0, z0, i0, j0, m_GridSize, m_GridSize);

	if (!IS_PASSABLE(m_Grid->get(i0, j0), passClass))
	{
		// The JPS pathfinder requires units to be on passable tiles
		// (otherwise it might crash), so handle the supposedly-invalid
		// state specially
		hierPath.FindNearestPassableNavcell(i0, j0, passClass);
	}

	state.goal = origGoal;

	// Make the goal reachable. This includes shortening the path if the goal is in a non-passable
	// region, transforming non-point goals to reachable point goals, etc.
	hierPath.MakeGoalReachable(i0, j0, state.goal, passClass);

	ENSURE(state.goal.type == PathGoal::POINT);

	// If we're already at the goal tile, then move directly to the exact goal coordinates
	if (state.goal.NavcellContainsGoal(i0, j0))
	{
		path.m_Waypoints.emplace_back(Waypoint{ state.goal.x, state.goal.z });
		return;
	}

	Pathfinding::NearestNavcell(state.goal.x, state.goal.z, state.iGoal, state.jGoal, m_GridSize, m_GridSize);

	ENSURE((state.goal.x / Pathfinding::NAVCELL_SIZE).ToInt_RoundToNegInfinity() == state.iGoal);
	ENSURE((state.goal.z / Pathfinding::NAVCELL_SIZE).ToInt_RoundToNegInfinity() == state.jGoal);

	state.passClass = passClass;

	state.steps = 0;

	state.tiles = new PathfindTileGrid(m_Grid->m_W, m_Grid->m_H);
	state.terrain = m_Grid;

	state.iBest = i0;
	state.jBest = j0;
	state.hBest = CalculateHeuristic(i0, j0, state.iGoal, state.jGoal);

	PriorityQueue::Item start = { TileID(i0, j0), PathCost() };
	state.open.push(start);
	state.tiles->get(i0, j0).SetStatusOpen();
	state.tiles->get(i0, j0).SetPred(i0, j0, i0, j0);
	state.tiles->get(i0, j0).SetCost(PathCost());

	while (true)
	{
		++state.steps;

		// If we ran out of tiles to examine, give up
		if (state.open.empty())
			break;

		// Move best tile from open to closed
		PriorityQueue::Item curr = state.open.pop();
		u16 i = curr.id.i();
		u16 j = curr.id.j();
		state.tiles->get(i, j).SetStatusClosed();

		// If we've reached the destination, stop
		if (state.goal.NavcellContainsGoal(i, j))
		{
			state.iBest = i;
			state.jBest = j;
			state.hBest = PathCost();
			break;
		}

		PathfindTile tile = state.tiles->get(i, j);
		PathCost g = tile.GetCost();

		// Get the direction of the predecessor tile from this tile
		int dpi = tile.GetPredDI();
		int dpj = tile.GetPredDJ();
		dpi = (dpi < 0 ? -1 : dpi > 0 ? 1 : 0);
		dpj = (dpj < 0 ? -1 : dpj > 0 ? 1 : 0);

		if (dpi != 0 && dpj == 0)
		{
			// Moving horizontally from predecessor
			if (!PASSABLE(i + dpi, j - 1))
			{
				AddJumpedDiag(i, j, -dpi, -1, g, state);
				AddJumpedVert(i, j, -1, g, state, OnTheWay(i, j, 0, -1, state.iGoal, state.jGoal));
			}
			if (!PASSABLE(i + dpi, j + 1))
			{
				AddJumpedDiag(i, j, -dpi, +1, g, state);
				AddJumpedVert(i, j, +1, g, state, OnTheWay(i, j, 0, +1, state.iGoal, state.jGoal));
			}
			AddJumpedHoriz(i, j, -dpi, g, state, OnTheWay(i, j, -dpi, 0, state.iGoal, state.jGoal));
		}
		else if (dpi == 0 && dpj != 0)
		{
			// Moving vertically from predecessor
			if (!PASSABLE(i - 1, j + dpj))
			{
				AddJumpedDiag(i, j, -1, -dpj, g, state);
				AddJumpedHoriz(i, j, -1, g, state,OnTheWay(i, j, -1, 0, state.iGoal, state.jGoal));
			}
			if (!PASSABLE(i + 1, j + dpj))
			{
				AddJumpedDiag(i, j, +1, -dpj, g, state);
				AddJumpedHoriz(i, j, +1, g, state,OnTheWay(i, j, +1, 0, state.iGoal, state.jGoal));
			}
			AddJumpedVert(i, j, -dpj, g, state, OnTheWay(i, j, 0, -dpj, state.iGoal, state.jGoal));
		}
		else if (dpi != 0 && dpj != 0)
		{
			// Moving diagonally from predecessor
			AddJumpedHoriz(i, j, -dpi, g, state, OnTheWay(i, j, -dpi, 0, state.iGoal, state.jGoal));
			AddJumpedVert(i, j, -dpj, g, state, OnTheWay(i, j, 0, -dpj, state.iGoal, state.jGoal));
			AddJumpedDiag(i, j, -dpi, -dpj, g, state);
		}
		else
		{
			// No predecessor, i.e. the start tile
			// Start searching in every direction

			// XXX - check passability?

			bool passl = PASSABLE(i - 1, j);
			bool passr = PASSABLE(i + 1, j);
			bool passd = PASSABLE(i, j - 1);
			bool passu = PASSABLE(i, j + 1);

			if (passl && passd)
				ProcessNeighbour(i, j, i-1, j-1, g, state);
			if (passr && passd)
				ProcessNeighbour(i, j, i+1, j-1, g, state);
			if (passl && passu)
				ProcessNeighbour(i, j, i-1, j+1, g, state);
			if (passr && passu)
				ProcessNeighbour(i, j, i+1, j+1, g, state);
			if (passl)
				ProcessNeighbour(i, j, i-1, j, g, state);
			if (passr)
				ProcessNeighbour(i, j, i+1, j, g, state);
			if (passd)
				ProcessNeighbour(i, j, i, j-1, g, state);
			if (passu)
				ProcessNeighbour(i, j, i, j+1, g, state);
		}
	}

	// Reconstruct the path (in reverse)
	u16 ip = state.iBest, jp = state.jBest;
	while (ip != i0 || jp != j0)
	{
		PathfindTile& n = state.tiles->get(ip, jp);
		entity_pos_t x, z;
		Pathfinding::NavcellCenter(ip, jp, x, z);
		path.m_Waypoints.emplace_back(Waypoint{ x, z });

		// Follow the predecessor link
		ip = n.GetPredI(ip);
		jp = n.GetPredJ(jp);
	}
	// The last waypoint is slightly incorrect (it's not the goal but the center
	// of the navcell of the goal), so replace it
	if (!path.m_Waypoints.empty())
		path.m_Waypoints.front() = { state.goal.x, state.goal.z };

	ImprovePathWaypoints(path, passClass, origGoal.maxdist, x0, z0);

	// Save this grid for debug display
	if (m_Debug.Overlay)
	{
		std::lock_guard<std::mutex> lock(g_DebugMutex);
		delete m_Debug.Grid;
		m_Debug.Grid = state.tiles;
		m_Debug.Steps = state.steps;
		m_Debug.Goal = state.goal;
	}
	else
		SAFE_DELETE(state.tiles);
}

#undef PASSABLE

void LongPathfinder::ImprovePathWaypoints(WaypointPath& path, pass_class_t passClass, entity_pos_t maxDist, entity_pos_t x0, entity_pos_t z0) const
{
	if (path.m_Waypoints.empty())
		return;

	if (maxDist > fixed::Zero())
	{
		CFixedVector2D start(x0, z0);
		CFixedVector2D first(path.m_Waypoints.back().x, path.m_Waypoints.back().z);
		CFixedVector2D offset = first - start;
		if (offset.CompareLength(maxDist) > 0)
		{
			offset.Normalize(maxDist);
			path.m_Waypoints.emplace_back(Waypoint{ (start + offset).X, (start + offset).Y });
		}
	}

	if (path.m_Waypoints.size() < 2)
		return;

	std::vector<Waypoint>& waypoints = path.m_Waypoints;
	std::vector<Waypoint> newWaypoints;

	CFixedVector2D prev(waypoints.front().x, waypoints.front().z);
	newWaypoints.push_back(waypoints.front());
	for (size_t k = 1; k < waypoints.size() - 1; ++k)
	{
		CFixedVector2D ahead(waypoints[k + 1].x, waypoints[k + 1].z);
		CFixedVector2D curr(waypoints[k].x, waypoints[k].z);

		if (maxDist > fixed::Zero() && (curr - prev).CompareLength(maxDist) > 0)
		{
			// We are too far away from the previous waypoint, so create one in
			// between and continue with the improvement of the path
			prev = prev + (curr - prev) / 2;
			newWaypoints.emplace_back(Waypoint{ prev.X, prev.Y });
		}

		// If we're mostly straight, don't even bother.
		if ((ahead - curr).Perpendicular().Dot(curr - prev).Absolute() <= fixed::Epsilon() * 100)
			continue;

		if (!Pathfinding::CheckLineMovement(prev.X, prev.Y, ahead.X, ahead.Y, passClass, *m_Grid))
		{
			prev = CFixedVector2D(waypoints[k].x, waypoints[k].z);
			newWaypoints.push_back(waypoints[k]);
		}
	}
	newWaypoints.push_back(waypoints.back());
	path.m_Waypoints.swap(newWaypoints);
}

void LongPathfinder::GetDebugDataJPS(u32& steps, double& time, Grid<u8>& grid) const
{
	steps = m_Debug.Steps;
	time = m_Debug.Time;

	if (!m_Debug.Grid)
		return;

	std::lock_guard<std::mutex> lock(g_DebugMutex);

	u16 iGoal, jGoal;
	Pathfinding::NearestNavcell(m_Debug.Goal.x, m_Debug.Goal.z, iGoal, jGoal, m_GridSize, m_GridSize);

	grid = Grid<u8>(m_Debug.Grid->m_W, m_Debug.Grid->m_H);
	for (u16 j = 0; j < grid.m_H; ++j)
	{
		for (u16 i = 0; i < grid.m_W; ++i)
		{
			if (i == iGoal && j == jGoal)
				continue;
			PathfindTile t = m_Debug.Grid->get(i, j);
			grid.set(i, j, (t.IsOpen() ? 1 : 0) | (t.IsClosed() ? 2 : 0));
		}
	}
}

void LongPathfinder::ComputePath(const HierarchicalPathfinder& hierPath, entity_pos_t x0, entity_pos_t z0, const PathGoal& origGoal,
    pass_class_t passClass, WaypointPath& path) const
{
	if (!m_Grid)
	{
		LOGERROR("The pathfinder grid hasn't been setup yet, aborting ComputeJPSPath");
		return;
	}

	ComputeJPSPath(hierPath, x0, z0, origGoal, passClass, path);
}
void LongPathfinder::ComputePath(const HierarchicalPathfinder& hierPath, entity_pos_t x0, entity_pos_t z0, const PathGoal& origGoal,
	pass_class_t passClass, std::vector<CircularRegion> excludedRegions, WaypointPath& path)
{
	GenerateSpecialMap(passClass, excludedRegions);
	ComputeJPSPath(hierPath, x0, z0, origGoal, SPECIAL_PASS_CLASS, path);
}

inline bool InRegion(u16 i, u16 j, CircularRegion region)
{
	fixed cellX = Pathfinding::NAVCELL_SIZE * i;
	fixed cellZ = Pathfinding::NAVCELL_SIZE * j;

	return CFixedVector2D(cellX - region.x, cellZ - region.z).CompareLength(region.r) <= 0;
}

void LongPathfinder::GenerateSpecialMap(pass_class_t passClass, std::vector<CircularRegion> excludedRegions)
{
	for (u16 j = 0; j < m_Grid->m_H; ++j)
	{
		for (u16 i = 0; i < m_Grid->m_W; ++i)
		{
			NavcellData n = m_Grid->get(i, j);
			if (!IS_PASSABLE(n, passClass))
			{
				n |= SPECIAL_PASS_CLASS;
				m_Grid->set(i, j, n);
				continue;
			}

			for (CircularRegion& region : excludedRegions)
			{
				if (!InRegion(i, j, region))
					continue;

				n |= SPECIAL_PASS_CLASS;
				break;
			}
			m_Grid->set(i, j, n);
		}
	}
}

/**
 * Terrain overlay for pathfinder debugging.
 * Renders a representation of the most recent pathfinding operation.
 */
class LongOverlay : public TerrainTextureOverlay
{
public:
	LongPathfinder& m_Pathfinder;

	LongOverlay(LongPathfinder& pathfinder) :
	TerrainTextureOverlay(Pathfinding::NAVCELLS_PER_TERRAIN_TILE), m_Pathfinder(pathfinder)
	{
	}

	virtual void BuildTextureRGBA(u8* data, size_t w, size_t h)
	{
		// Grab the debug data for the most recently generated path
		u32 steps;
		double time;
		Grid<u8> debugGrid;
		m_Pathfinder.GetDebugData(steps, time, debugGrid);

		// Render navcell passability
		u8* p = data;
		for (size_t j = 0; j < h; ++j)
		{
			for (size_t i = 0; i < w; ++i)
			{
				SColor4ub color(0, 0, 0, 0);
				if (!IS_PASSABLE(m_Pathfinder.m_Grid->get((int)i, (int)j), m_Pathfinder.m_Debug.PassClass))
					color = SColor4ub(255, 0, 0, 127);

				if (debugGrid.m_W && debugGrid.m_H)
				{
					u8 n = debugGrid.get((int)i, (int)j);

					if (n == 1)
						color = SColor4ub(255, 255, 0, 127);
					else if (n == 2)
						color = SColor4ub(0, 255, 0, 127);

					if (m_Pathfinder.m_Debug.Goal.NavcellContainsGoal(i, j))
						color = SColor4ub(0, 0, 255, 127);
				}

				*p++ = color.R;
				*p++ = color.G;
				*p++ = color.B;
				*p++ = color.A;
			}
		}

		// Render the most recently generated path
		if (m_Pathfinder.m_Debug.Path && !m_Pathfinder.m_Debug.Path->m_Waypoints.empty())
		{
			std::vector<Waypoint>& waypoints = m_Pathfinder.m_Debug.Path->m_Waypoints;
			u16 ip = 0, jp = 0;
			for (size_t k = 0; k < waypoints.size(); ++k)
			{
				u16 i, j;
				Pathfinding::NearestNavcell(waypoints[k].x, waypoints[k].z, i, j, m_Pathfinder.m_GridSize, m_Pathfinder.m_GridSize);
				if (k == 0)
				{
					ip = i;
					jp = j;
				}
				else
				{
					bool firstCell = true;
					do
					{
						if (data[(jp*w + ip)*4+3] == 0)
						{
							data[(jp*w + ip)*4+0] = 0xFF;
							data[(jp*w + ip)*4+1] = 0xFF;
							data[(jp*w + ip)*4+2] = 0xFF;
							data[(jp*w + ip)*4+3] = firstCell ? 0xA0 : 0x60;
						}
						ip = ip < i ? ip+1 : ip > i ? ip-1 : ip;
						jp = jp < j ? jp+1 : jp > j ? jp-1 : jp;
						firstCell = false;
					}
					while (ip != i || jp != j);
				}
			}
		}
	}
};

// These two functions must come below LongOverlay's definition.
void LongPathfinder::SetDebugOverlay(bool enabled)
{
	if (enabled && !m_Debug.Overlay)
		m_Debug.Overlay = new LongOverlay(*this);
	else if (!enabled && m_Debug.Overlay)
		SAFE_DELETE(m_Debug.Overlay);
}

LongPathfinder::~LongPathfinder()
{
	SAFE_DELETE(m_Debug.Overlay);
	SAFE_DELETE(m_Debug.Grid);
	SAFE_DELETE(m_Debug.Path);
}
