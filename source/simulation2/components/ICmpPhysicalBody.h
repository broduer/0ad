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

#ifndef INCLUDED_ICMPPHYSICALBODY
#define INCLUDED_ICMPPHYSICALBODY

#include "simulation2/helpers/Position.h"
#include "simulation2/system/Interface.h"

#include <cstdint>

/**
 * A unit's physical body: its weight, whether it takes part in pushing, and how crowded it is
 * (pressure). The pushing itself is computed by UnitCollision and the motion manager.
 */
class ICmpPhysicalBody : public IComponent
{
public:
	/**
	 * Set whether this unit takes part in pushing.
	 */
	virtual void SetEnabled(bool enabled) = 0;

	virtual bool IsPushingEnabled() const = 0;

	/**
	 * Heavier units push harder and are harder to push.
	 */
	virtual entity_pos_t GetWeight() const = 0;

	/**
	 * An ad-hoc counter of how much pushing 'pressure' the unit is under.
	 * More pressure slows the unit down and makes it harder to push,
	 * which effectively bogs down groups of colliding units.
	 */
	virtual uint8_t GetPressure() const = 0;

	virtual void SetPressure(uint8_t pressure) = 0;

	/**
	 * Add to the pressure, up to its maximum.
	 */
	virtual void AddPressure(uint8_t pressure) = 0;

	DECLARE_INTERFACE_TYPE(PhysicalBody)
};

#endif // INCLUDED_ICMPPHYSICALBODY
