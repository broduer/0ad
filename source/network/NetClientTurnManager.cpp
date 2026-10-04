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

#include "NetClient.h"
#include "NetClientTurnManager.h"
#include "lib/debug.h"
#include "lib/os_path.h"
#include "lib/path.h"
#include "lib/utf8.h"
#include "network/NetMessage.h"
#include "ps/CLogger.h"
#include "ps/Profile.h"
#include "ps/Pyrogenesis.h"
#include "ps/Replay.h"
#include "ps/Util.h"
#include "simulation2/Simulation2.h"

#include <cstddef>
#include <fstream>
#include <js/RootingAPI.h>
#include <sstream>
#include <string>

#if 0
#define NETCLIENTTURN_LOG(...) debug_printf(__VA_ARGS__)
#else
#define NETCLIENTTURN_LOG(...)
#endif

extern CStrW g_UniqueLogPostfix;

CNetClientTurnManager::CNetClientTurnManager(CSimulation2& simulation, CNetClient& client, int clientId, IReplayLogger& replay)
	: CTurnManager(simulation, DEFAULT_TURN_LENGTH, COMMAND_DELAY_MP, clientId, replay), m_NetClient(client)
{
}

void CNetClientTurnManager::PostCommand(JS::HandleValue data)
{
	NETCLIENTTURN_LOG("PostCommand()\n");

	// Transmit command to server
	CSimulationMessage msg(m_Simulation2.GetScriptInterface(), m_ClientId, m_PlayerId, m_CurrentTurn + m_CommandDelay, data);
	m_NetClient.SendMessage(&msg);

	// Add to our local queue
	//AddCommand(m_ClientId, m_PlayerId, data, m_CurrentTurn + m_CommandDelay);
	// TODO: we should do this when the server stops sending our commands back to us
}

void CNetClientTurnManager::NotifyFinishedOwnCommands(turn_id_t turn)
{
	NETCLIENTTURN_LOG("NotifyFinishedOwnCommands(%d)\n", turn);

	CEndCommandBatchMessage msg;

	msg.m_Turn = turn;

	// The turn-length field of the CEndCommandBatchMessage is currently only relevant
	// when sending it from the server to the clients.
	// It could be used to verify that the client simulated the correct turn length.
	msg.m_TurnLength = 0;

	m_NetClient.SendMessage(&msg);
}

void CNetClientTurnManager::NotifyFinishedUpdate(turn_id_t turn, const UpdateCallback&)
{
	bool quick = !TurnNeedsFullHash(turn);
	std::string hash;
	{
		PROFILE3("state hash check");
		ENSURE(m_Simulation2.ComputeStateHash(hash, quick));
	}

	NETCLIENTTURN_LOG("NotifyFinishedUpdate(%d, %hs)\n", turn, Hexify(hash).c_str());

	m_Replay.Hash(hash, quick);

	// Send message to the server
	CSyncCheckMessage msg;
	msg.m_Turn = turn;
	msg.m_Hash = hash;
	m_NetClient.SendMessage(&msg);

	if (m_PendingOosDump && turn >= m_PendingOosDump->dumpTurn)
		DumpOosState();
}

void CNetClientTurnManager::OnDestroyConnection()
{
	// Attempt to flush messages before leaving.
	// Notice the sending is not reliable and rarely makes it to the Server.
	if (m_NetClient.GetCurrState() == NCS_INGAME)
		NotifyFinishedOwnCommands(m_CurrentTurn + m_CommandDelay);
}

void CNetClientTurnManager::CancelOosDump()
{
	if (m_PendingOosDump)
	{
		LOGWARNING("OOS diagnosis for turn %u cancelled before dump turn %u was reached",
			m_PendingOosDump->oosTurn, m_PendingOosDump->dumpTurn);
		m_PendingOosDump.reset();
	}
}

void CNetClientTurnManager::OnSimulationMessage(CSimulationMessage* msg)
{
	// Command received from the server - store it for later execution
	AddCommand(msg->m_Client, msg->m_Player, msg->m_Data, static_cast<turn_id_t>(msg->m_Turn));
}

void CNetClientTurnManager::OnSyncError(turn_id_t turn, turn_id_t dumpTurn, const CStr& expectedHash, const std::vector<CSyncErrorMessage::S_m_PlayerNames>& playerNames)
{
	CStr expectedHashHex(Hexify(expectedHash));
	NETCLIENTTURN_LOG("OnSyncError(%d, %hs)\n", turn, expectedHashHex.c_str());

	std::string hash;
	ENSURE(m_Simulation2.ComputeStateHash(hash, !TurnNeedsFullHash(turn)));

	OsPath oosdumpPath(psLogDir() / (L"oos_dump" + g_UniqueLogPostfix + L".txt"));
	// D must not have been authorized yet, even if several updates run between polls.
	const bool dumpPending = dumpTurn > m_ReadyTurn;
	if (dumpPending)
	{
		if (m_PendingOosDump && (m_PendingOosDump->oosTurn != turn || m_PendingOosDump->dumpTurn != dumpTurn))
			LOGWARNING("OOS diagnosis for turn %u superseded by turn %u before its dump was taken",
				m_PendingOosDump->oosTurn, turn);
		m_PendingOosDump = PendingOosDump{turn, dumpTurn};
	}
	else
		LOGERROR("OOS dump turn %u was already authorized; not dumping a different turn", dumpTurn);

	std::stringstream playerNamesString;
	std::vector<CStr> playerNamesStrings;
	playerNamesStrings.reserve(playerNames.size());
	for (size_t i = 0; i < playerNames.size(); ++i)
	{
		CStr name = utf8_from_wstring(playerNames[i].m_Name);
		playerNamesString << (i == 0 ? "" : ", ") << name;
		playerNamesStrings.push_back(name);
	}

	LOGERROR("Out-Of-Sync on turn {}\nPlayers: {}\nDiagnostic dump requested for turn {}: {}",
		turn, playerNamesString.str(), dumpTurn, oosdumpPath.string8());

	m_NetClient.PushGuiMessage(
		"type", "out-of-sync",
		"turn", turn,
		"dumpTurn", dumpTurn,
		"dumpPending", dumpPending,
		"players", playerNamesStrings,
		"expectedHash", expectedHashHex,
		"hash", Hexify(hash),
		"path_oos_dump", wstring_from_utf8(oosdumpPath.string8()),
		"path_replay", wstring_from_utf8(m_Replay.GetDirectory().string8()));
}

void CNetClientTurnManager::DumpOosState()
{
	const PendingOosDump pending = *m_PendingOosDump;
	m_PendingOosDump.reset();
	if (m_CurrentTurn != pending.dumpTurn)
	{
		LOGERROR("OOS dump turn %u was missed; not dumping turn %u", pending.dumpTurn, m_CurrentTurn);
		return;
	}

	const OsPath path(psLogDir() / (L"oos_dump" + g_UniqueLogPostfix + L".txt"));
	std::ofstream file(OsString(path), std::ofstream::out | std::ofstream::trunc);
	file << "oos turn: " << pending.oosTurn << std::endl;
	file << "dump turn: " << pending.dumpTurn << std::endl;
	file << "net client turn: " << m_CurrentTurn << std::endl;
	const bool textOk = m_Simulation2.DumpDebugState(file);
	file.close();

	std::ofstream binfile(OsString(path.ChangeExtension(L".dat")),
		std::ofstream::out | std::ofstream::trunc | std::ofstream::binary);
	const bool binaryOk = m_Simulation2.SerializeState(binfile);
	binfile.close();
	if (!textOk || !binaryOk || !file || !binfile)
		LOGERROR("Failed to write OOS diagnostic artifacts for turn %u", pending.dumpTurn);
	else
		LOGERROR("Out-Of-Sync on turn %u: dumped diagnostic turn %u to %s",
			pending.oosTurn, pending.dumpTurn, path.string8());
}
