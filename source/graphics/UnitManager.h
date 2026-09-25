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

/*
 * Container that owns all units
 */

#ifndef INCLUDED_UNITMANAGER
#define INCLUDED_UNITMANAGER

#include "lib/posix/posix_types.h"
#include "simulation2/system/Entity.h"

#include <cstdint>
#include <memory>
#include <vector>

class CObjectManager;
class CStrW;
class CUnit;

///////////////////////////////////////////////////////////////////////////////
// CUnitManager: simple container class holding all units within the world
class CUnitManager
{
public:
	// constructor, destructor
	CUnitManager();
	~CUnitManager();

	// add given unit to world
	CUnit* AddUnit(std::unique_ptr<CUnit> unit);
	// remove given unit from world and delete it
	void DeleteUnit(CUnit* unit);

	// creates a new unit and adds it to the world
	CUnit* CreateUnit(const CStrW& actorName, const entity_id_t id, const uint32_t seed);

	void SetObjectManager(CObjectManager& objectManager) { m_ObjectManager = &objectManager; }

	/**
	 * Seed the random visual choices the simulation does not depend on, such
	 * as animation variants. A game sets it from its settings so replays look
	 * the same; otherwise it is random.
	 */
	void SetVisualSeed(std::uint32_t seed) { m_VisualSeed = seed; }
	std::uint32_t GetVisualSeed() const { return m_VisualSeed; }

	/**
	 * Mark a specific region of the terrain as dirty.
	 * Coordinates are in terrain tiles, lower inclusive, upper exclusive.
	 */
	void MakeTerrainDirty(ssize_t i0, ssize_t j0, ssize_t i1, ssize_t j1, int dirtyFlags);

private:
	// list of all known units
	std::vector<std::unique_ptr<CUnit>> m_Units;
	// graphical object manager; may be NULL if not set up
	CObjectManager* m_ObjectManager;
	std::uint32_t m_VisualSeed;
};

#endif // INCLUDED_UNITMANAGER
