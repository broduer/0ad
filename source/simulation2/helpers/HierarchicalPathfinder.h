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

#ifndef INCLUDED_HIERPATHFINDER
#define INCLUDED_HIERPATHFINDER

#include "Pathfinding.h"

#include "graphics/SColor.h"
#include "lib/code_annotation.h"
#include "lib/debug.h"
#include "ps/CLogger.h"
#include "renderer/TerrainOverlay.h"

#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace RenderWorld { struct CFramePacket; }
class CSimContext;
class PathGoal;
struct SOverlayLine;
template <typename T> class Grid;

/**
 * Hierarchical pathfinder.
 *
 * Deals with connectivity (can point A reach point B?).
 *
 * The navcell-grid representation of the map is split into fixed-size chunks.
 * Within a chunk, each maximal set of adjacently-connected passable navcells
 * is defined as a region.
 * Each region is a vertex in the hierarchical pathfinder's graph.
 * When two regions in adjacent chunks are connected by passable navcells,
 * the graph contains an edge between the corresponding two vertexes.
 * By design, there can never be an edge between two regions in the same chunk.
 *
 * Those fixed-size chunks are used to efficiently compute "global regions" by effectively flood-filling.
 * Those can then be used to immediately determine if two reachables points are connected.
 *
 * The main use of this class is to convert an arbitrary PathGoal to a reachable navcell.
 * This happens in MakeGoalReachable.
 *
 */

#ifdef TEST
class TestCmpPathfinder;
class TestHierarchicalPathfinder;
#endif

class HierarchicalPathfinder
{
#ifdef TEST
	friend class TestCmpPathfinder;
	friend class TestHierarchicalPathfinder;
#endif
public:
	typedef std::uint32_t GlobalRegionID;

	struct RegionID
	{
		std::uint8_t ci, cj; // chunk ID
		std::uint16_t r; // unique-per-chunk local region ID

		RegionID(std::uint8_t ci, std::uint8_t cj, std::uint16_t r) : ci(ci), cj(cj), r(r) { }

		bool operator<(const RegionID& b) const
		{
			// Sort by chunk ID, then by per-chunk region ID
			if (ci < b.ci)
				return true;
			if (b.ci < ci)
				return false;
			if (cj < b.cj)
				return true;
			if (b.cj < cj)
				return false;
			return r < b.r;
		}

		bool operator==(const RegionID& b) const
		{
			return ((ci == b.ci) && (cj == b.cj) && (r == b.r));
		}

		// Returns the distance from the center to the point (i, j)
		inline std::uint32_t DistanceTo(std::uint16_t i, std::uint16_t j) const
		{
			return (ci * CHUNK_SIZE + CHUNK_SIZE/2 - i) * (ci * CHUNK_SIZE + CHUNK_SIZE/2 - i) +
			       (cj * CHUNK_SIZE + CHUNK_SIZE/2 - j) * (cj * CHUNK_SIZE + CHUNK_SIZE/2 - j);
		}

	};

	HierarchicalPathfinder();
	~HierarchicalPathfinder();

	void SetDebugOverlay(bool enabled, const CSimContext* simContext);

	// Non-pathfinding grids will never be recomputed on calling HierarchicalPathfinder::Update
	void Recompute(Grid<NavcellData>* passabilityGrid,
		const std::map<std::string, pass_class_t>& nonPathfindingPassClassMasks,
		const std::map<std::string, pass_class_t>& pathfindingPassClassMasks);

	void Update(Grid<NavcellData>* grid, const Grid<std::uint8_t>& dirtinessGrid);

	RegionID Get(std::uint16_t i, std::uint16_t j, pass_class_t passClass) const;

	GlobalRegionID GetGlobalRegion(std::uint16_t i, std::uint16_t j, pass_class_t passClass) const;
	GlobalRegionID GetGlobalRegion(RegionID region, pass_class_t passClass) const;

	/**
	 * Updates @p goal so that it's guaranteed to be reachable from the navcell
	 * @p i0, @p j0 (which is assumed to be on a passable navcell).
	 *
	 * If the goal is not reachable, it is replaced with a point goal nearest to
	 * the goal center.
	 *
	 * In the case of a non-point reachable goal, it is replaced with a point goal
	 * at the reachable navcell of the goal which is nearest to the starting navcell.
	 *
	 * @returns true if the goal was reachable, false otherwise.
	 */
	bool MakeGoalReachable(std::uint16_t i0, std::uint16_t j0, PathGoal& goal,
		pass_class_t passClass) const;

	/**
	 * @return true if the goal is reachable from navcell i0, j0.
	 * (similar to MakeGoalReachable but only checking for reachability).
	 */
	bool IsGoalReachable(std::uint16_t i0, std::uint16_t j0, const PathGoal& goal,
		pass_class_t passClass) const;

	/**
	 * Updates @p i, @p j (which is assumed to be an impassable navcell)
	 * to the nearest passable navcell.
	 */
	void FindNearestPassableNavcell(std::uint16_t& i, std::uint16_t& j, pass_class_t passClass) const;

	/**
	 * Generates the connectivity grid associated with the given pass_class
	 */
	Grid<std::uint16_t> GetConnectivityGrid(pass_class_t passClass) const;

	pass_class_t GetPassabilityClass(const std::string& name) const
	{
		auto it = m_PassClassMasks.find(name);
		if (it != m_PassClassMasks.end())
			return it->second;

		LOGERROR("Invalid passability class name '%s'", name.c_str());
		return 0;
	}

	/**
	 * Record the debug lines and region overlay for the render world while
	 * enabled, and their removal once disabled.
	 */
	void PublishDebugOverlay(RenderWorld::CFramePacket& packet, std::ptrdiff_t tilesPerSide);

private:
	static const std::uint8_t CHUNK_SIZE = 96; // number of navcells per side
									 // TODO: figure out best number. Probably 64 < n < 128

	struct Chunk
	{
		std::uint8_t m_ChunkI, m_ChunkJ; // chunk ID
		std::vector<std::uint16_t> m_RegionsID; // IDs of local regions, 0 (impassable) excluded
		std::uint16_t m_Regions[CHUNK_SIZE][CHUNK_SIZE]; // local region ID per navcell

		cassert(CHUNK_SIZE*CHUNK_SIZE/2 < 65536); // otherwise we could overflow m_RegionsID with a checkerboard pattern

		void InitRegions(int ci, int cj, Grid<NavcellData>* grid, pass_class_t passClass);

		RegionID Get(int i, int j) const;

		void RegionCenter(std::uint16_t r, int& i, int& j) const;

		void RegionNavcellNearest(std::uint16_t r, int iGoal, int jGoal, int& iBest, int& jBest,
			std::uint32_t& dist2Best) const;

		bool RegionNearestNavcellInGoal(std::uint16_t r, std::uint16_t i0, std::uint16_t j0,
			const PathGoal& goal, std::uint16_t& iOut, std::uint16_t& jOut,
			std::uint32_t& dist2Best) const;

#ifdef TEST
		bool operator==(const Chunk& b) const
		{
			return (m_ChunkI == b.m_ChunkI && m_ChunkJ == b.m_ChunkJ &&
				m_RegionsID.size() == b.m_RegionsID.size() && memcmp(&m_Regions, &b.m_Regions,
				sizeof(std::uint16_t) * CHUNK_SIZE * CHUNK_SIZE) == 0);
		}
#endif
	};

	const Chunk& GetChunk(std::uint8_t ci, std::uint8_t cj, pass_class_t passClass) const
	{
		return m_Chunks.at(passClass).at(cj * m_ChunksW + ci);
	}

	typedef std::map<RegionID, std::set<RegionID> > EdgesMap;

	void ComputeNeighbors(EdgesMap& edges, Chunk& a, Chunk& b, bool transpose, bool opposite) const;
	void RecomputeAllEdges(pass_class_t passClass, EdgesMap& edges);
	void UpdateEdges(std::uint8_t ci, std::uint8_t cj, pass_class_t passClass, EdgesMap& edges);

	void UpdateGlobalRegions(const std::map<pass_class_t, std::vector<RegionID> >& needNewGlobalRegionMap);

	/**
	 * Returns all reachable regions, optionally ordered in a specific manner.
	 */
	template<typename Ordering>
	void FindReachableRegions(RegionID from, std::set<RegionID, Ordering>& reachable, pass_class_t passClass) const
	{
		// Flood-fill the region graph, starting at 'from',
		// collecting all the regions that are reachable via edges
		reachable.insert(from);

		const EdgesMap& edgeMap = m_Edges.at(passClass);
		if (edgeMap.find(from) == edgeMap.end())
			return;

		std::vector<RegionID> open;
		open.reserve(64);
		open.push_back(from);

		while (!open.empty())
		{
			RegionID curr = open.back();
			open.pop_back();

			for (const RegionID& region : edgeMap.at(curr))
				// Add to the reachable set; if this is the first time we added
				// it then also add it to the open list
				if (reachable.insert(region).second)
					open.push_back(region);
		}
	}

	struct SortByCenterToPoint
	{
		SortByCenterToPoint(std::uint16_t i, std::uint16_t j): gi(i), gj(j) {};
		bool operator()(const HierarchicalPathfinder::RegionID& a, const HierarchicalPathfinder::RegionID& b) const
		{
			if (a.DistanceTo(gi, gj) < b.DistanceTo(gi, gj))
				return true;
			if (a.DistanceTo(gi, gj) > b.DistanceTo(gi, gj))
				return false;
			return a.r < b.r;
		}
		std::uint16_t gi, gj;
	};

	void FindNearestNavcellInRegions(const std::set<RegionID, SortByCenterToPoint>& regions,
		std::uint16_t& iGoal, std::uint16_t& jGoal, pass_class_t passClass) const;

	struct InterestingRegion {
		RegionID region;
		std::uint16_t bestI;
		std::uint16_t bestJ;
	};

	struct SortByBestToPoint
	{
		SortByBestToPoint(std::uint16_t i, std::uint16_t j): gi(i), gj(j) {};
		bool operator()(const InterestingRegion& a, const InterestingRegion& b) const
		{
			if ((a.bestI - gi) * (a.bestI - gi) + (a.bestJ - gj) * (a.bestJ - gj) < (b.bestI - gi) * (b.bestI - gi) + (b.bestJ - gj) * (b.bestJ - gj))
				return true;
			if ((a.bestI - gi) * (a.bestI - gi) + (a.bestJ - gj) * (a.bestJ - gj) > (b.bestI - gi) * (b.bestI - gi) + (b.bestJ - gj) * (b.bestJ - gj))
				return false;
			return a.region.r < b.region.r;
		}
		std::uint16_t gi, gj;
	};

	// Returns the region along with the best cell for optimisation.
	void FindGoalRegionsAndBestNavcells(std::uint16_t i0, std::uint16_t j0, std::uint16_t gi,
		std::uint16_t gj, const PathGoal& goal, std::set<InterestingRegion,
		SortByBestToPoint>& regions, pass_class_t passClass) const;

	void FillRegionOnGrid(const RegionID& region, pass_class_t passClass, std::uint16_t value,
		Grid<std::uint16_t>& grid) const;

	std::uint16_t m_W, m_H;
	std::uint8_t m_ChunksW, m_ChunksH;
	std::map<pass_class_t, std::vector<Chunk> > m_Chunks;

	std::map<pass_class_t, EdgesMap> m_Edges;

	std::map<pass_class_t, std::map<RegionID, GlobalRegionID> > m_GlobalRegions;
	GlobalRegionID m_NextGlobalRegionID;

	// Passability classes for which grids will be updated when calling Update
	std::map<std::string, pass_class_t> m_PassClassMasks;

	void AddDebugEdges(pass_class_t passClass);
	bool m_DebugOverlayEnabled{false};
	bool m_DebugOverlayPublished{false};
	const CSimContext* m_SimContext; // Used for drawing the debug lines

public:
	std::vector<SOverlayLine> m_DebugOverlayLines;
};
#endif // INCLUDED_HIERPATHFINDER
