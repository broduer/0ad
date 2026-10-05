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

#ifndef INCLUDED_MOTIONSTATE
#define INCLUDED_MOTIONSTATE

#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "simulation2/system/Entity.h"
#include "simulation2/helpers/Position.h"  // For entity_angle_t

#include <optional>

class ICmpObstruction;
class ICmpPosition;
class CCmpUnitMotion;
class ICmpPhysicalBody;

/**
 * Motion state structure used by UnitMotionManager to track unit movement state.
 * This is stored as a separate header so it can be used by multiple components
 * without causing circular dependencies.
 *
 * NB: This struct holds raw component pointers which is generally dangerous,
 * but the tight coupling with CCmpUnitMotion makes it workable.
 * NB: This assumes that components do NOT move in memory,
 * which is currently a fair assumption but might change in the future.
 * NB: Only plain values belong here, nothing that owns memory (no std::vector, no std::string):
 * EntityMap moves and overwrites its entries as raw bytes.
 */
struct MotionState
{
	MotionState(
		ICmpPosition* cmpPos,
		CCmpUnitMotion* cmpMotion,
		ICmpPhysicalBody* cmpPhysicalBody);

	// Component references, set by the manager when the unit registers and after deserialization.
	// These must be kept alive for the duration of motion.
	ICmpPosition* cmpPosition;
	CCmpUnitMotion* cmpUnitMotion;
	// Null for an entity without that component.
	ICmpPhysicalBody* cmpPhysicalBody;
	ICmpObstruction* cmpObstruction = nullptr;
	// Position before units start moving
	CFixedVector2D initialPos;
	// Transient position during the movement.
	CFixedVector2D pos;

	fixed speed;

	entity_angle_t initialAngle;
	entity_angle_t angle;

	// Used for formations - units with the same control group won't push at a distance.
	// (this is required because formations may be tight and large units may end up never settling.
	entity_id_t controlGroup = INVALID_ENTITY;

	// Meta-flag -> this entity won't push nor be pushed.
	// (used for entities that have their obstruction disabled).
	bool ignore = false;

	// If true, the entity needs to be handled during movement.
	bool needUpdate = false;

	bool wentStraight = false;
	bool wasObstructed = false;
	// Clone of the obstruction manager flag for efficiency
	bool isMoving = false;
	// The value of isMoving last passed to the Obstruction component, which is only told when it changes.
	// Empty until it has been told once, which includes after deserialization.
	std::optional<bool> movingFlag;
	// What this turn's queued impulses (collision pushes) add up to, accumulated as they are
	// consumed and applied further down the same update. Transient, so not serialized: the queue is drained every turn.
	CFixedVector2D impulseForce;
	uint8_t impulsePressure = 0;
};

#endif // INCLUDED_MOTIONSTATE