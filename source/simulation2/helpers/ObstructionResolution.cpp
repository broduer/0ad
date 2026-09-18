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
#include "ObstructionResolution.h"

#include "simulation2/components/ICmpObstructionManager.h"
#include "simulation2/components/ICmpPathfinder.h"

namespace
{
/**
 * Sidesteps tried, smallest first, when a path-following unit is blocked: up to two navcells, as a larger
 * sideways hop in a single turn would be visible.
 */
constexpr fixed NUDGE_DISTANCES[]{
	fixed::FromFraction(Pathfinding::NAVCELL_SIZE_INT, 2),
	fixed::FromInt(Pathfinding::NAVCELL_SIZE_INT),
	fixed::FromFraction(Pathfinding::NAVCELL_SIZE_INT * 3, 2),
	fixed::FromInt(Pathfinding::NAVCELL_SIZE_INT * 2)
};
}

bool ObstructionResolution::TrySlide(CFixedVector2D& pos, const CFixedVector2D& movement, const entity_id_t entityId,
	const ICmpPathfinder& pathfinder, const IObstructionTestFilter& filter,
	const entity_pos_t clearance, const pass_class_t passClass)
{
	const CFixedVector2D slidePositive{CFixedVector2D(movement.X - movement.Y, movement.X + movement.Y) / 2};
	const CFixedVector2D slideNegative{CFixedVector2D(movement.X + movement.Y, movement.Y - movement.X) / 2};

	const bool preferPositive{entityId % 2 == 0};
	const CFixedVector2D slides[]{
		preferPositive ? slidePositive : slideNegative,
		preferPositive ? slideNegative : slidePositive};

	for (const CFixedVector2D& slide : slides)
	{
		const CFixedVector2D slideTarget{pos + slide};
		if (pathfinder.CheckMovement(filter, pos.X, pos.Y, slideTarget.X, slideTarget.Y, clearance, passClass))
		{
			pos = slideTarget;
			return true;
		}
	}
	return false;
}

bool ObstructionResolution::TryPerpendicularNudge(CFixedVector2D& pos, const CFixedVector2D& movement,
	const CFixedVector2D& waypoint, const ICmpPathfinder& pathfinder, const IObstructionTestFilter& filter,
	const entity_pos_t clearance, const pass_class_t passClass)
{
	if (movement.IsZero())
		return false;

	// Move to the candidate if we can get there, and still go straight on to the waypoint from it.
	const auto tryMoveTo = [&](const CFixedVector2D& candidate) -> bool
	{
		if (!pathfinder.CheckMovement(filter, pos.X, pos.Y, candidate.X, candidate.Y, clearance, passClass) ||
			!pathfinder.CheckMovement(filter, candidate.X, candidate.Y, waypoint.X, waypoint.Y, clearance, passClass))
			return false;
		pos = candidate;
		return true;
	};

	CFixedVector2D moveDir{movement};
	moveDir.Normalize();
	const CFixedVector2D leftDir{moveDir.Perpendicular()};
	const CFixedVector2D rightDir{-leftDir};

	// Try the side the waypoint lies on first.
	const bool waypointOnLeft{(waypoint - pos).Dot(leftDir) >= fixed::Zero()};
	const CFixedVector2D firstDir{waypointOnLeft ? leftDir : rightDir};
	const CFixedVector2D secondDir{-firstDir};

	for (const fixed nudgeDist : NUDGE_DISTANCES)
	{
		const CFixedVector2D firstNudge{firstDir.Multiply(nudgeDist)};
		const CFixedVector2D secondNudge{secondDir.Multiply(nudgeDist)};

		// Sideways while still making the move.
		if (tryMoveTo(pos + movement + firstNudge) || tryMoveTo(pos + movement + secondNudge))
			return true;

		// Only sideways, in case the move itself is what's blocked.
		if (nudgeDist >= Pathfinding::NAVCELL_SIZE && (tryMoveTo(pos + firstNudge) || tryMoveTo(pos + secondNudge)))
			return true;
	}

	return false;
}
