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

#ifndef INCLUDED_SIMCONTEXT
#define INCLUDED_SIMCONTEXT

#include "lib/debug.h"
#include "simulation2/system/Entity.h"

#include <memory>

class CComponentManager;
class CUnitManager;
class CTerrain;
namespace RenderWorld { struct CFramePacket; struct SFeedback; }
namespace Script { class Interface; }

/**
 * Contains pointers to various 'global' objects that are needed by the simulation code,
 * to allow easy access without using real (evil) global variables.
 */
class CSimContext
{
public:
	CSimContext(CTerrain* terrain = nullptr, CUnitManager* unitManager = nullptr);
	~CSimContext();

	CComponentManager& GetComponentManager() const;
	void SetComponentManager(CComponentManager* man);

	bool HasUnitManager() const;
	CUnitManager& GetUnitManager() const;

	CTerrain& GetTerrain() const;

	/**
	 * Return the packet the components write the render world's records into
	 * while the simulation is displayed, or null.
	 */
	RenderWorld::CFramePacket* GetFramePacket() const { return m_FramePacket; }
	void SetFramePacket(RenderWorld::CFramePacket* packet) { m_FramePacket = packet; }

	/**
	 * Return what the render world last reported about the units it drew,
	 * fixed for a logic step, or null while nothing is displayed.
	 */
	const RenderWorld::SFeedback* GetRenderFeedback() const { return m_RenderFeedback.get(); }
	void SetRenderFeedback(std::shared_ptr<const RenderWorld::SFeedback> feedback);

	Script::Interface& GetScriptInterface() const;

	void SetSystemEntity(CEntityHandle ent) { m_SystemEntity = ent; }
	CEntityHandle GetSystemEntity() const { ASSERT(m_SystemEntity.GetId() == SYSTEM_ENTITY); return m_SystemEntity; }

	/**
	 * Returns the player ID that the current display is being rendered for.
	 * Currently relies on g_Game being initialised (evil globals...)
	 */
	int GetCurrentDisplayedPlayer() const;

private:
	CComponentManager* m_ComponentManager{nullptr};
	CUnitManager* m_UnitManager;
	CTerrain* m_Terrain;
	RenderWorld::CFramePacket* m_FramePacket{nullptr};
	std::shared_ptr<const RenderWorld::SFeedback> m_RenderFeedback;

	CEntityHandle m_SystemEntity;
};

#endif // INCLUDED_SIMCONTEXT
