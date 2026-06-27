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

#ifndef INCLUDED_REPLAYTURNMANAGER
#define INCLUDED_REPLAYTURNMANAGER

#include "lib/types.h"
#include "ps/CStr.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/system/LocalTurnManager.h"

#include <js/ValueArray.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

class CSimulation2;
class IReplayLogger;

/**
 * Implementation of CLocalTurnManager for replay games.
 */
class CReplayTurnManager : public CLocalTurnManager
{
public:
	CReplayTurnManager(CSimulation2& simulation, IReplayLogger& replay);

	void StoreReplayCommand(turn_id_t turn, int player, const std::string& command);

	void StoreReplayTurnLength(turn_id_t turn, u32 turnLength);

	void StoreReplayHash(turn_id_t turn, const std::string& hash, bool quick);

	void StoreFinalReplayTurn(turn_id_t turn);

private:
	void NotifyFinishedUpdate(turn_id_t turn, const UpdateCallback& sendEventToAll) override;

	void DoTurn(turn_id_t turn, const UpdateCallback& sendEventToAll);

	static const CStr EventNameReplayFinished;
	static const CStr EventNameReplayOutOfSync;

	bool m_HasSyncError = false;

	// Contains the commands of every player on each turn
	std::map<turn_id_t, std::vector<std::pair<player_id_t, std::string>>> m_ReplayCommands;

	// Contains the length of every turn
	std::map<turn_id_t, u32> m_ReplayTurnLengths;

	// Contains all replay hash values and weather or not the quick hash method was used
	std::map<turn_id_t, std::pair<std::string, bool>> m_ReplayHash;
};

#endif // INCLUDED_REPLAYTURNMANAGER
