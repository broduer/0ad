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

#ifndef INCLUDED_PATH_UTILS
#define INCLUDED_PATH_UTILS

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/helpers/Pathfinding.h"

#include <vector>

/**
 * Helper class for walking along paths and performing segment-based operations.
 * Provides utilities for projecting points onto paths, finding distances along paths,
 * and moving forward/backward along path segments.
 */
class PathUtils
{
public:
	/**
	 * Calculate normalized direction vector from segment start to end.
	 */
	static CFixedVector2D GetSegmentDirection(const std::vector<Waypoint>& path, size_t segment)
	{
		if (segment >= path.size() - 1)
			return CFixedVector2D(fixed::Zero(), fixed::FromInt(1));

		CFixedVector2D start(path[segment].x, path[segment].z);
		CFixedVector2D end(path[segment + 1].x, path[segment + 1].z);
		return CalculateNormalizedDirection(end - start);
	}

	/**
	 * Calculate length of a path segment.
	 */
	static entity_pos_t GetSegmentLength(const std::vector<Waypoint>& path, size_t segment)
	{
		if (segment >= path.size() - 1)
			return fixed::Zero();

		CFixedVector2D start(path[segment].x, path[segment].z);
		CFixedVector2D end(path[segment + 1].x, path[segment + 1].z);
		return (end - start).Length();
	}

	/**
	 * Project a point onto a line segment.
	 */
	static bool ProjectPointOntoSegment(
		const CFixedVector2D& point,
		const CFixedVector2D& segmentStart,
		const CFixedVector2D& segmentEnd,
		CFixedVector2D& projectedPoint,
		entity_pos_t& projectionFactor,
		entity_pos_t& distanceFromSegmentStart)
	{
		CFixedVector2D segmentVec = segmentEnd - segmentStart;
		entity_pos_t segLengthSq = segmentVec.Dot(segmentVec);

		if (segLengthSq <= fixed::Epsilon())
			return false;

		CFixedVector2D toPoint = point - segmentStart;
		projectionFactor = toPoint.Dot(segmentVec) / segLengthSq;
		projectionFactor = Clamp(projectionFactor, fixed::Zero(), fixed::FromInt(1));

		projectedPoint = segmentStart + segmentVec.Multiply(projectionFactor);
		distanceFromSegmentStart = projectionFactor.Multiply(segmentVec.Length());

		return true;
	}

	/**
	 * Find the closest point on a path to a given position.
	 * Uses Manhattan distance to avoid fixed-point overflow.
	 */
	static bool FindClosestPointOnPath(
		const std::vector<Waypoint>& path,
		const CFixedVector2D& startPos,
		size_t& outSegment,
		CFixedVector2D& outPoint,
		entity_pos_t& outDistanceAlong)
	{
		if (path.empty())
			return false;

		// Search all waypoints starting from index 0
		size_t closestIdx = 0;
		entity_pos_t closestManhattan = entity_pos_t::FromInt(INT_MAX);

		// Search all waypoints
		for (size_t i = 0; i < path.size(); ++i)
		{
			CFixedVector2D waypoint(path[i].x, path[i].z);
			CFixedVector2D diff = waypoint - startPos;
			entity_pos_t manhattan = diff.X.Absolute() + diff.Y.Absolute();

			if (manhattan < closestManhattan)
			{
				closestManhattan = manhattan;
				closestIdx = i;
			}

			// Near exact match - early exit
			if (manhattan <= fixed::FromInt(1))
			{
				closestIdx = i;
				break;
			}
		}

		// Set results
		outSegment = (closestIdx < path.size() - 1) ? closestIdx : path.size() - 2;
		outPoint = CFixedVector2D(path[closestIdx].x, path[closestIdx].z);
		outDistanceAlong = (closestIdx == path.size() - 1) ?
			GetSegmentLength(path, path.size() - 2) : fixed::Zero();

		return true;
	}

	/**
	 * Walk forward along a path from a given point.
	 */
	static bool MoveForward(
		const std::vector<Waypoint>& path,
		size_t& segment,
		CFixedVector2D& point,
		entity_pos_t& distanceAlong,
		entity_pos_t& remainingDistance,
		CFixedVector2D& outPoint,
		CFixedVector2D& outDirection)
	{
		while (segment < path.size() - 1)
		{
			CFixedVector2D segStart(path[segment].x, path[segment].z);
			CFixedVector2D segEnd(path[segment + 1].x, path[segment + 1].z);
			entity_pos_t segmentLength = (segEnd - segStart).Length();
			entity_pos_t remainingInSegment = segmentLength - distanceAlong;

			if (remainingDistance <= remainingInSegment)
			{
				// Destination within current segment
				outDirection = CalculateNormalizedDirection(segEnd - segStart);
				outPoint = point + outDirection.Multiply(remainingDistance);
				return true;
			}

			// Move to next segment
			remainingDistance -= remainingInSegment;
			segment++;
			point = segEnd;
			distanceAlong = fixed::Zero();
		}

		return false; // Reached end of path
	}

	/**
	 * Walk backward along a path from a given point.
	 */
	static bool MoveBackward(
		const std::vector<Waypoint>& path,
		size_t& segment,
		CFixedVector2D& point,
		entity_pos_t& distanceAlong,
		entity_pos_t& remainingDistance,
		CFixedVector2D& outPoint,
		CFixedVector2D& outDirection)
	{
		while (true)
		{
			if (segment == 0 && distanceAlong <= fixed::Zero())
				return false; // Reached start of path

			entity_pos_t availableInSegment = distanceAlong;
			if (remainingDistance <= availableInSegment)
			{
				// Destination within current segment
				CFixedVector2D segStart(path[segment].x, path[segment].z);
				CFixedVector2D segEnd(path[segment + 1].x, path[segment + 1].z);
				outDirection = CalculateNormalizedDirection(segEnd - segStart);
				outPoint = point - outDirection.Multiply(remainingDistance);
				return true;
			}

			// Move to previous segment
			remainingDistance -= availableInSegment;
			if (segment > 0)
			{
				segment--;
				CFixedVector2D segStart(path[segment].x, path[segment].z);
				CFixedVector2D segEnd(path[segment + 1].x, path[segment + 1].z);
				distanceAlong = (segEnd - segStart).Length();
				point = segStart;
			}
			else
			{
				distanceAlong = fixed::Zero();
			}
		}
	}

	/**
	 * Extrapolate beyond the end of a path.
	 */
	static bool ExtrapolateForward(
		const std::vector<Waypoint>& path,
		entity_pos_t remainingDistance,
		CFixedVector2D& outPoint,
		CFixedVector2D& outDirection)
	{
		size_t lastSegment = path.size() - 2;
		CFixedVector2D segStart(path[lastSegment].x, path[lastSegment].z);
		CFixedVector2D segEnd(path[lastSegment + 1].x, path[lastSegment + 1].z);
		outDirection = CalculateNormalizedDirection(segEnd - segStart);
		outPoint = segEnd + outDirection.Multiply(remainingDistance);
		return true;
	}

	/**
	 * Extrapolate before the start of a path.
	 */
	static bool ExtrapolateBackward(
		const std::vector<Waypoint>& path,
		entity_pos_t remainingDistance,
		CFixedVector2D& outPoint,
		CFixedVector2D& outDirection)
	{
		CFixedVector2D segStart(path[0].x, path[0].z);
		CFixedVector2D segEnd(path[1].x, path[1].z);
		outDirection = CalculateNormalizedDirection(segEnd - segStart);
		outPoint = segStart - outDirection.Multiply(remainingDistance);
		return true;
	}

	/**
	 * Move along a path by a specified distance from a starting point.
	 */
	static bool MoveAlongPath(
		const std::vector<Waypoint>& path,
		size_t startSegment,
		const CFixedVector2D& startPoint,
		entity_pos_t startDistanceAlong,
		entity_pos_t totalDistance,
		CFixedVector2D& outPoint,
		CFixedVector2D& outDirection)
	{
		const bool movingForward = totalDistance >= fixed::Zero();
		entity_pos_t remainingDistance = totalDistance.Absolute();
		size_t currentSegment = startSegment;
		CFixedVector2D currentPoint = startPoint;
		entity_pos_t currentDistanceAlong = startDistanceAlong;

		while (remainingDistance > fixed::Zero())
		{
			if (movingForward)
			{
				if (!MoveForward(path, currentSegment, currentPoint, currentDistanceAlong,
								 remainingDistance, outPoint, outDirection))
				{
					return ExtrapolateForward(path, remainingDistance, outPoint, outDirection);
				}
				return true;
			}
			else
			{
				if (!MoveBackward(path, currentSegment, currentPoint, currentDistanceAlong,
								  remainingDistance, outPoint, outDirection))
				{
					return ExtrapolateBackward(path, remainingDistance, outPoint, outDirection);
				}
				return true;
			}
		}

		outPoint = currentPoint;
		outDirection = GetSegmentDirection(path, currentSegment);
		return true;
	}

	/**
	 * Find a point along a path at a given distance from the start.
	 */
	static bool FindPointAlongPath(
		const std::vector<Waypoint>& path,
		const CFixedVector2D& startPos,
		entity_pos_t distance,
		CFixedVector2D& resultPoint,
		CFixedVector2D& resultDirection)
	{
		// Handle trivial cases
		if (path.empty())
		{
			resultPoint = startPos;
			resultDirection = CFixedVector2D(fixed::Zero(), fixed::FromInt(1));
			return false;
		}

		if (path.size() == 1)
		{
			resultPoint = CFixedVector2D(path[0].x, path[0].z);
			resultDirection = CFixedVector2D(fixed::Zero(), fixed::FromInt(1));
			return true;
		}

		// Find closest point on path to start position
		size_t bestSegment;
		CFixedVector2D closestPoint;
		entity_pos_t distanceAlongSegment;

		if (!FindClosestPointOnPath(path, startPos, bestSegment, closestPoint, distanceAlongSegment))
		{
			resultPoint = startPos;
			resultDirection = CFixedVector2D(fixed::Zero(), fixed::FromInt(1));
			return false;
		}

		// If distance is zero, return closest point
		if (distance.Absolute() <= fixed::Epsilon())
		{
			resultPoint = closestPoint;
			resultDirection = GetSegmentDirection(path, bestSegment);
			return true;
		}

		// Walk along path by specified distance
		return MoveAlongPath(path, bestSegment, closestPoint, distanceAlongSegment,
							 distance, resultPoint, resultDirection);
	}

	/**
	 * Project a point backward along a path.
	 */
	static bool ProjectBackwardAlongPath(
		const std::vector<Waypoint>& path,
		const CFixedVector2D& startPos,
		entity_pos_t distance,
		size_t segment,
		entity_pos_t distanceAlong,
		CFixedVector2D& outPoint,
		CFixedVector2D& outDirection)
	{
		entity_pos_t remainingDistance = distance;
		size_t currentSegment = segment;
		entity_pos_t currentDistance = distanceAlong;

		// Walk backward along the path
		while (currentSegment > 0 && remainingDistance > fixed::Zero())
		{
			if (currentDistance > fixed::Zero())
			{
				entity_pos_t segmentAvailable = currentDistance;

				if (segmentAvailable >= remainingDistance)
				{
					// Enough in this segment
					currentDistance = currentDistance - remainingDistance;

					// Calculate the point
					CFixedVector2D segStart(path[currentSegment].x, path[currentSegment].z);
					CFixedVector2D segEnd(path[currentSegment + 1].x, path[currentSegment + 1].z);
					CFixedVector2D segDir = CalculateNormalizedDirection(segEnd - segStart);
					outPoint = segStart + segDir.Multiply(currentDistance);
					outDirection = segDir;
					return true;
				}
				else
				{
					// Use entire current segment
					remainingDistance = remainingDistance - segmentAvailable;
					currentSegment--;
					currentDistance = (currentSegment > 0) ? GetSegmentLength(path, currentSegment) : fixed::Zero();
				}
			}
			else
			{
				// Move to previous segment
				currentSegment--;
				currentDistance = (currentSegment > 0) ? GetSegmentLength(path, currentSegment) : fixed::Zero();
			}
		}

		// Not enough path behind, extrapolate from last segment
		if (path.size() >= 2)
		{
			CFixedVector2D segStart(path[0].x, path[0].z);
			CFixedVector2D segEnd(path[1].x, path[1].z);
			outDirection = CalculateNormalizedDirection(segEnd - segStart);
			outPoint = segStart - outDirection.Multiply(remainingDistance);
		}
		else
		{
			// Fallback - should not happen since we check path.size() >= 2 in caller
			outPoint = startPos;
			outDirection = CFixedVector2D(fixed::Zero(), fixed::FromInt(1));
		}

		return true;
	}

private:
	/**
	 * Helper to calculate normalized direction.
	 */
	static CFixedVector2D CalculateNormalizedDirection(const CFixedVector2D& vec)
	{
		if (vec.Dot(vec) <= fixed::Epsilon())
			return CFixedVector2D(fixed::Zero(), fixed::FromInt(1));

		CFixedVector2D result = vec;
		result.Normalize();
		return result;
	}
};

#endif // INCLUDED_PATH_UTILS