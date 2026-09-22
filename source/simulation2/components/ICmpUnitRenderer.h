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

#ifndef INCLUDED_ICMPMODELRENDERER
#define INCLUDED_ICMPMODELRENDERER

#include "maths/Vector3D.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Interface.h"

#include <js/Value.h>
#include <utility>
#include <vector>

class CEntityHandle;

/**
 * The simulation's side of unit rendering: the render world draws the units,
 * this keeps their LOS visibility current and answers picking queries from
 * what the render world last reported.
 */
class ICmpUnitRenderer : public IComponent
{
public:
	/**
	 * Register an entity whose unit the render world draws.
	 */
	virtual void AddUnit(CEntityHandle entity) = 0;

	virtual void RemoveUnit(entity_id_t entity) = 0;

	/**
	 * Recompute the entity's visibility before the next frame, as it moved or left the world.
	 */
	virtual void MarkVisibilityDirty(entity_id_t entity) = 0;

	/**
	 * Return a list of visual entities along with their center point.
	 * Visual means they have an associated actor and a visual component,
	 * but they could still be hiden in the fog of war for a specific player,
	 * for example.
	 * NOTE: It's generally faster to do a lot of ray intersection tests than
	 * querying a lot of entities for component interfaces and doing these types
	 * of tests first.
	 */
	virtual void PickAllEntitiesAtPoint(std::vector<std::pair<CEntityHandle, CVector3D> >& outEntities,
		const CVector3D& origin, const CVector3D& dir,
		bool allowEditorSelectables) const = 0;

	/**
	 * Toggle the rendering of debug info.
	 */
	virtual void SetDebugOverlay(bool enabled) = 0;

	DECLARE_INTERFACE_TYPE(UnitRenderer)
};

#endif // INCLUDED_ICMPMODELRENDERER
