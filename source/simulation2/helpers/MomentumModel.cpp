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
#include "MomentumModel.h"

#include <algorithm>

CFixedVector2D MomentumModel::PredictPosition(const CFixedVector2D& pos, const CFixedVector2D& pathVelocity,
	const CFixedVector2D& inertiaVelocity, const fixed inertiaStrength, const fixed maxSpeed, const fixed dt)
{
	// Look ahead as far as it takes us to stop: speed·dt·(1 + inertia) is the sum of the exponential
	// decay, i.e. 1 + inertia turns.
	const fixed lookaheadTurns{fixed::FromInt(1) + inertiaStrength};

	CFixedVector2D predictedPos{pos};
	CFixedVector2D remainingInertia{inertiaVelocity};
	for (fixed t{fixed::Zero()}; t < lookaheadTurns; t += fixed::FromInt(1))
	{
		const CFixedVector2D turnVelocity{ClampSpeed(BlendWithInertia(pathVelocity, remainingInertia, inertiaStrength), maxSpeed)};
		predictedPos = predictedPos + turnVelocity.Multiply(dt);

		// Inertia carries over as the velocity we achieved
		remainingInertia = turnVelocity;
	}
	return predictedPos;
}

bool MomentumModel::CorrectDrift(CFixedVector2D& velocity, const CFixedVector2D& inertiaVelocity,
	const CFixedVector2D& forwardDir, const fixed driftCorrection, const fixed maxSpeed)
{
	const CFixedVector2D rightDir{forwardDir.Perpendicular()};

	const fixed forwardComponent{velocity.Dot(forwardDir)};
	const fixed lateralComponent{velocity.Dot(rightDir)};
	if (lateralComponent.IsZero())
		return false;

	const fixed newLateralComponent{lateralComponent - lateralComponent.Multiply(driftCorrection)};

	// Cancelling sideways drift costs forward speed. There's no cost if the unit has no sideways momentum (e.g. its
	// path simply turned), and the cost is never more than the sideways speed actually cancelled.
	const fixed inertiaLateral{inertiaVelocity.Dot(rightDir)};
	const fixed energyCost{inertiaLateral.IsZero() ? fixed::Zero() :
		std::min(inertiaLateral.Absolute(), lateralComponent.Absolute()).Multiply(driftCorrection)};

	const fixed newForwardComponent{std::max(forwardComponent - energyCost, fixed::Zero())};

	velocity = forwardDir.Multiply(newForwardComponent) + rightDir.Multiply(newLateralComponent);

	// Our own cost brought us to a standstill - deliberate, not an obstruction.
	const bool stopped{velocity.IsZero()};

	velocity = ClampSpeed(velocity, maxSpeed);
	return stopped;
}
