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

#ifndef INCLUDED_ICMPIMPULSEQUEUE
#define INCLUDED_ICMPIMPULSEQUEUE

#include "simulation2/system/Interface.h"
#include "simulation2/helpers/Position.h"

#include <cstdint>
#include <vector>

/**
 * A kick queued for a unit: a linear displacement for this turn, a velocity change, pressure, and spin/roll impulses.
 * Collected from various sources (collision pushes, knockback...) and consumed in a coordinated
 * phase during UnitMotionManager::Move.
 */
struct ImpulseRequest
{
	ImpulseRequest() = default;
	ImpulseRequest(const entity_id_t t, const CFixedVector2D& f, const uint8_t p = 0,
		const fixed a = fixed::Zero(), const fixed r = fixed::Zero())
		: target{t}, force{f}, pressureIncrease{p}, angularImpulse{a}, rollImpulse{r} {}

	entity_id_t target{INVALID_ENTITY};
	CFixedVector2D force;          // Displacement this turn only (collision pushes)
	CFixedVector2D velocityChange; // Added to the unit's velocity (m/s), which then carries on as momentum (hits)
	uint8_t pressureIncrease{0};
	fixed angularImpulse{fixed::Zero()}; // Y-axis rotation impulse (spinning/turning) from a hit; rings down by spinDecay
	fixed rollImpulse{fixed::Zero()};    // X-axis rotation impulse (tilting/rolling)
	// Spin (radians/second) from a collision push landing off the unit's centre: the angular half of `force`,
	// so it is carried on exactly as the push is, decaying by Drifting rather than spinDecay. A push is
	// recomputed every turn two units overlap, so on spinDecay this spin would pile up turn after turn
	// even for a unit whose pushes are one-shot (Drifting 0): a blob of cavalry wound itself up to the
	// spin cap. Using Drifting, it can only build up where the push does (e.g. on a ship).
	fixed pushSpin{fixed::Zero()};
};

class ICmpImpulseQueue : public IComponent
{
public:
    DECLARE_INTERFACE_TYPE(ImpulseQueue)

    virtual void AddImpulse(const ImpulseRequest& request) = 0;
    virtual void AddImpulses(const std::vector<ImpulseRequest>& requests) = 0;

    // JS-exposed method(s)
    // Push the target as a hit: the impulse, divided by the target's weight, is a velocity change in m/s.
    virtual void AddWeightDampedImpulse(entity_id_t target, entity_pos_t impulseX, entity_pos_t impulseY, uint8_t pressureIncrease) = 0;

    virtual std::vector<ImpulseRequest> ConsumeImpulses() = 0;
};

#endif // INCLUDED_ICMPIMPULSEQUEUE