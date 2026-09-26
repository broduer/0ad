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

#include "precompiled.h"

#include "NetTurnStats.h"

#include "lib/utf8.h"

#include <fmt/format.h>
#include <string>

namespace
{
enum class Column
{
	Player,
	ReadyTurn,
	SimulatedTurn,
	SimBehindTurns,
	SimBehindMs,
	StallShort,
	StallLong
};
} // anonymous namespace

CStr CNetTurnStatsTable::GetName()
{
	return "net turns";
}

CStr CNetTurnStatsTable::GetTitle()
{
	const std::lock_guard<std::mutex> lock{m_Mutex};
	// Each stall is blamed on a single client, so the sums are the game's total stall.
	std::uint32_t stallMsShort{0};
	std::uint32_t stallMsLong{0};
	for (const ClientRow& client : m_Clients)
	{
		stallMsShort += client.stallMsShort;
		stallMsLong += client.stallMsLong;
	}
	return fmt::format("Network turn lag (server ready turn: {}, turn length: {} ms, stall 20s: {:.1f} s, 5min: {:.1f} s)",
		m_ServerReadyTurn, m_TurnLength, stallMsShort / 1000.0, stallMsLong / 1000.0);
}

size_t CNetTurnStatsTable::GetNumberRows()
{
	const std::lock_guard<std::mutex> lock{m_Mutex};
	return m_Clients.size();
}

const std::vector<ProfileColumn>& CNetTurnStatsTable::GetColumns()
{
	if (m_ColumnDescriptions.empty())
	{
		m_ColumnDescriptions.emplace_back("Player", 200);
		m_ColumnDescriptions.emplace_back("Ready turn", 90);
		m_ColumnDescriptions.emplace_back("Simulated", 90);
		m_ColumnDescriptions.emplace_back("Behind (turns)", 110);
		m_ColumnDescriptions.emplace_back("Behind (ms)", 100);
		m_ColumnDescriptions.emplace_back("Stall 20s (s)", 110);
		m_ColumnDescriptions.emplace_back("Stall 5min (s)", 120);
	}
	return m_ColumnDescriptions;
}

CStr CNetTurnStatsTable::GetCellText(size_t row, size_t col)
{
	const std::lock_guard<std::mutex> lock{m_Mutex};
	if (row >= m_Clients.size())
		return "???";

	const ClientRow& client{m_Clients[row]};
	const turn_id_t simBehind{m_ServerReadyTurn - client.simulatedTurn};

	switch (static_cast<Column>(col))
	{
	case Column::Player:
	{
		const std::string name{client.playerName.empty() ?
			fmt::format("client {}", client.hostID) : utf8_from_wstring(client.playerName)};
		return client.isObserver ? name + " (observer)" : name;
	}
	case Column::ReadyTurn:
		return std::to_string(client.readyTurn);
	case Column::SimulatedTurn:
		return std::to_string(client.simulatedTurn);
	case Column::SimBehindTurns:
		return std::to_string(simBehind);
	case Column::SimBehindMs:
		return std::to_string(static_cast<std::int64_t>(simBehind) * m_TurnLength);
	case Column::StallShort:
		return fmt::format("{:.1f}", client.stallMsShort / 1000.0);
	case Column::StallLong:
		return fmt::format("{:.1f}", client.stallMsLong / 1000.0);
	default:
		return "???";
	}
}

AbstractProfileTable* CNetTurnStatsTable::GetChild(size_t /*row*/)
{
	return 0;
}

void CNetTurnStatsTable::Snapshot(const turn_id_t serverReadyTurn, const std::uint32_t turnLength, const std::vector<ClientRow>& clients)
{
	const std::lock_guard<std::mutex> lock{m_Mutex};
	m_ServerReadyTurn = serverReadyTurn;
	m_TurnLength = turnLength;
	m_Clients = clients;
}
