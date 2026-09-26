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

#ifndef INCLUDED_NETTURNSTATS
#define INCLUDED_NETTURNSTATS

#include "lib/code_annotation.h"
#include "ps/CStr.h"
#include "ps/ProfileViewer.h"
#include "simulation2/helpers/SimulationCommand.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

/**
 * Profiler table showing, for each client, how far behind the server's
 * ready turn it is, both in receiving commands and in simulating, and how
 * long the game recently stalled waiting on its progress.
 *
 * Thread-safety:
 * - Must be constructed in the main thread (to match the profiler).
 * - Snapshot is called from the network server thread; the displayed
 *   copy of the data is protected by a mutex.
 */
class CNetTurnStatsTable : public AbstractProfileTable
{
	NONCOPYABLE(CNetTurnStatsTable);
public:
	struct ClientRow
	{
		int hostID;
		CStrW playerName;
		bool isObserver;
		int playerID;
		turn_id_t readyTurn;
		turn_id_t simulatedTurn;
		// Time the game spent waiting on this client's progress in the last 20 s / 5 min.
		std::uint32_t stallMsShort;
		std::uint32_t stallMsLong;
	};

	CNetTurnStatsTable() = default;

	CStr GetName() override;
	CStr GetTitle() override;
	size_t GetNumberRows() override;
	const std::vector<ProfileColumn>& GetColumns() override;
	CStr GetCellText(size_t row, size_t col) override;
	AbstractProfileTable* GetChild(size_t row) override;

	void Snapshot(turn_id_t serverReadyTurn, std::uint32_t turnLength, const std::vector<ClientRow>& clients);

private:
	std::vector<ProfileColumn> m_ColumnDescriptions;

	std::mutex m_Mutex;
	turn_id_t m_ServerReadyTurn{0}; // protected by m_Mutex
	std::uint32_t m_TurnLength{0}; // protected by m_Mutex
	std::vector<ClientRow> m_Clients; // protected by m_Mutex
};

#endif // INCLUDED_NETTURNSTATS
