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
 */
struct MotionState
{
	MotionState(
		ICmpPosition* cmpPos,
		CCmpUnitMotion* cmpMotion,
		ICmpPhysicalBody* cmpPhysicalBody);

	// Component references - these must be kept alive for the duration of motion.
	ICmpPosition* cmpPosition;
	CCmpUnitMotion* cmpUnitMotion;
	ICmpPhysicalBody* cmpPhysicalBody;

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
	// A waypoint was popped although the straight line to the next target is obstructed
	// (it couldn't be reached without circling back), so a short path should be requested.
	bool poppedBlockedCorner = false;

	// Whether a collision was predicted last turn. Carried over between turns (not reset in PreMove):
	// the steering velocity is computed before the prediction runs, so it can only use the previous result.
	bool predictedCollision = false;

	// Clone of the obstruction manager flag for efficiency
	bool isMoving = false;
	// What this turn's queued impulses (collision pushes, knockback) add up to, accumulated as they are
	// consumed and applied further down the same update. Transient, so not serialized: the queue is drained every turn.
	CFixedVector2D impulseForce;     // Displacement for this turn
	CFixedVector2D impulseVelocity;  // Velocity change
	uint8_t impulsePressure = 0;

	CFixedVector2D inertiaVelocity;         // Our own momentum (last turn's movement minus the external part); steers our facing
	CFixedVector2D externalInertiaVelocity; // Momentum from pushes and hits, carried over; doesn't affect our facing
	CFixedVector2D externalVelocity;        // This turn's velocity from queued impulses

	// Angular velocity from pathfinding (existing)
	fixed angularVelocity;

	// A heading the unit keeps turning towards, with angular momentum, while it has no move request: an attack target,
	// or its destination on arrival. Set by FaceTowardsPoint, cleared once aligned.
	bool hasFaceTarget = false;
	entity_angle_t faceTargetAngle;

	// Angular velocity from hits, kept apart from angularVelocity so it doesn't interfere with pathfinding turning
	fixed impactAngularVelocity;
	fixed spinDecay;  // Fraction of impactAngularVelocity lost every IMPACT_REFERENCE_TIME

	// Roll effect (rotation around forward axis)
	// Positive = tilt to the right, Negative = tilt to the left
	fixed rollAngle;           // Current roll angle in radians
	fixed rollVelocity;        // How fast the roll is changing (radians/sec)
	fixed maxRollAngle;        // Maximum roll angle for this unit (0 = no roll)
};

#endif // INCLUDED_MOTIONSTATE