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

#include "NetStats.h"

#include "network/NetClientSession.h"
#include "network/NetServerSession.h"

#include <fmt/format.h>
#include <ngtcp2/ngtcp2.h>
#include <string>

enum
{
	Row_InData,
	Row_OutData,
	Row_LastSendTime,
	Row_LastRecvTime,
	Row_NextTimeout,
	Row_PacketsSent,
	Row_PacketsLost,
	Row_LastRTT,
	Row_RTT,
	Row_MTU,
	Row_ReliableInTransit,
	NumberRows
};

CNetStatsTable::CNetStatsTable(const CNetClientSession& conn):
	m_Connection{&conn}
{
}

CStr CNetStatsTable::GetName()
{
	return "net";
}

CStr CNetStatsTable::GetTitle()
{
	if (m_Connection)
		return "Network client statistics";
	else
		return "Network host statistics";
}

size_t CNetStatsTable::GetNumberRows()
{
	return NumberRows;
}

const std::vector<ProfileColumn>& CNetStatsTable::GetColumns()
{
	m_ColumnDescriptions.clear();
	m_ColumnDescriptions.push_back(ProfileColumn("Name", 200));

	if (m_Connection)
		m_ColumnDescriptions.push_back(ProfileColumn("Value", 80));
	else
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		for (size_t i = 0; i < m_LatchedData.size(); ++i)
			m_ColumnDescriptions.push_back(ProfileColumn(fmt::format("Peer {}", i), 80));
	}

	return m_ColumnDescriptions;
}

CStr CNetStatsTable::GetCellText(size_t row, size_t col)
{
	// Return latched data, if we have any
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (col > 0 && m_LatchedData.size() > col-1 && m_LatchedData[col-1].size() > row)
			return m_LatchedData[col-1][row];
	}

	#define ROW(id, title, member) \
	case id: \
		if (col == 0) return title; \
		if (m_Connection) return std::to_string(member); \
		return "???"

	CNetClientSession::ProfileData info;
	if (col != 0)
		info = m_Connection->GetProfileData();

	switch(row)
	{
	ROW(Row_InData, "incoming bytes", info.connectionInfo.bytes_recv);
	ROW(Row_OutData, "outgoing bytes", info.connectionInfo.bytes_sent);
	ROW(Row_LastSendTime, "last send time", std::chrono::duration_cast<std::chrono::milliseconds>(
		info.lastSendTime.time_since_epoch()).count());
	ROW(Row_LastRecvTime, "last receive time", std::chrono::duration_cast<std::chrono::milliseconds>(
		info.lastReceiveTime.time_since_epoch()).count());
	ROW(Row_NextTimeout, "next timeout", info.expiry);
	ROW(Row_PacketsSent, "packets sent", info.connectionInfo.pkt_sent);
	ROW(Row_PacketsLost, "packets lost", info.connectionInfo.pkt_lost);
	ROW(Row_LastRTT, "last RTT", info.connectionInfo.latest_rtt);
	ROW(Row_RTT, "mean RTT", info.connectionInfo.smoothed_rtt);
	ROW(Row_MTU, "MTU", info.maxPayloadSize);
	ROW(Row_ReliableInTransit, "reliable data in transit", info.connectionInfo.bytes_in_flight);

	default:
		return "???";
	}

#undef ROW
}

AbstractProfileTable* CNetStatsTable::GetChild(size_t /*row*/)
{
	return 0;
}

void CNetStatsTable::LatchHostState(const std::span<std::unique_ptr<CNetServerSession>> sessions)
{
	std::lock_guard<std::mutex> lock(m_Mutex);

#define ROW(id, title, member) \
	m_LatchedData[i].push_back(std::to_string(info.member));

	m_LatchedData.clear();
	m_LatchedData.resize(sessions.size());

	for (size_t i = 0; i < sessions.size(); ++i)
	{
		CNetServerSession::ProfileData info{sessions[i]->GetProfileData()};
		ROW(Row_InData, "incoming bytes", connectionInfo.bytes_recv);
		ROW(Row_OutData, "outgoing bytes", connectionInfo.bytes_sent);
		m_LatchedData[i].push_back(std::to_string(
			std::chrono::duration_cast<std::chrono::milliseconds>(
			info.lastSendTime.time_since_epoch()).count()));
		m_LatchedData[i].push_back(std::to_string(
			std::chrono::duration_cast<std::chrono::milliseconds>(
			info.lastReceiveTime.time_since_epoch()).count()));
		ROW(Row_NextTimeout, "next timeout", expiry);
		ROW(Row_PacketsSent, "packets sent", connectionInfo.pkt_sent);
		ROW(Row_PacketsLost, "packets lost", connectionInfo.pkt_lost);
		ROW(Row_LastRTT, "last RTT", connectionInfo.latest_rtt);
		ROW(Row_RTT, "mean RTT", connectionInfo.smoothed_rtt);
		ROW(Row_MTU, "MTU", maxPayloadSize);
		ROW(Row_ReliableInTransit, "reliable data in transit", connectionInfo.bytes_in_flight);
	}
#undef ROW
}
