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

#ifndef INCLUDED_ICMPATTACK
#define INCLUDED_ICMPATTACK

#include "maths/Fixed.h"
#include "ps/CStr.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Interface.h"

#include <js/Value.h>
#include <string>
#include <vector>

class ICmpAttack : public IComponent
{
public:
    virtual float GetRepeatTime(const std::string& type) const = 0;
    virtual std::vector<CStr> GetAttackTypes() const = 0;

    /**
     * The speed (m/s) this entity has to be moving at for a contact to count as a charge, or a negative
     * value if it has no Attack/ChargeAttack and cannot charge. Slower contacts are ordinary bumps.
     */
    virtual fixed GetChargeMinSpeed() const = 0;

    /**
     * This entity, charging, has run into @p target. The velocity each side lost or gained in the collision
     * is given in m/s; @p fullBlowSpeed is the speed at which a hit deals the whole ChargeAttack damage.
     * Called from inside the motion update: the damage is dealt at the end of the turn.
     */
    virtual void OnChargeContact(entity_id_t target, fixed targetSpeedChange, fixed chargerSpeedChange,
        fixed fullBlowSpeed) = 0;

    /**
     * Deal the Attack/ChargeAttack effects to a target hit by this entity's charge.
     * @param target - The entity that was hit.
     * @param fraction - How much of a full blow the hit was (0..1); scales the ChargeAttack effects.
     */
    virtual void PerformChargeHit(entity_id_t target, fixed fraction) = 0;
    DECLARE_INTERFACE_TYPE(Attack)
};

#endif // INCLUDED_ICMPATTACK
