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

#ifndef INCLUDED_NETSERVERTURNMANAGER
#define INCLUDED_NETSERVERTURNMANAGER

#include "lib/code_annotation.h"
#include "network/NetTurnStats.h"
#include "ps/CStr.h"
#include "simulation2/helpers/SimulationCommand.h"

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class CNetServerSession;
class CNetServerWorker;

/**
 * The server-side counterpart to CNetClientTurnManager.
 * Records the turn state of each client, and sends turn advancement messages
 * when clients are ready.
 *
 * Thread-safety:
 * - This is constructed and used by CNetServerWorker in the network server thread.
 */
class CNetServerTurnManager
{
	NONCOPYABLE(CNetServerTurnManager);
public:
	CNetServerTurnManager(CNetServerWorker& server, float gameSpeed);

	void NotifyFinishedClientCommands(CNetServerSession& session, turn_id_t turn);

	void NotifyFinishedClientUpdate(CNetServerSession& session, turn_id_t turn, const CStr& hash);

	/**
	 * Inform the turn manager of a new client
	 * @param observer - whether this client is an observer.
	 * @param playerID - the player slot of this client, -1 if it has none.
	 */
	void InitialiseClient(int client, turn_id_t turn, bool observer, int playerID);

	/**
	 * Inform the turn manager that a previously-initialised client has left the game.
	 */
	void UninitialiseClient(int client);

	void SetTurnLength(std::uint32_t msecs);

	/**
	 * Inform the turn manager that the game was paused or unpaused,
	 * so the time until the next turn release isn't counted as stall.
	 */
	void NotifyPauseChanged();

	/**
	 * Returns the latest turn for which all clients are ready;
	 * they will have already been told to execute this turn.
	 */
	turn_id_t GetReadyTurn() { return m_ReadyTurn; }

	/**
	 * Returns the turn length that was used for the given turn.
	 * Requires turn <= GetReadyTurn().
	 */
	std::uint32_t GetSavedTurnLength(turn_id_t turn);

	/**
	 * Copy the per-client turn state into the given profiler table.
	 */
	void SnapshotStats(CNetTurnStatsTable& table);

private:
	/**
	 * Release the next turn if all clients are ready for it.
	 * @param lastClient - the client whose change triggered this check.
	 */
	void CheckClientsReady(int lastClient);

	/**
	 * Record how late the turn that was just released is, blaming the client
	 * whose progress through its turns arrived last.
	 */
	void UpdateStallBlame(int lastClient);

	struct Client
	{
		CStrW playerName;
		// Latest turn for which all commands have been received.
		turn_id_t readyTurn;
		// Last known simulated turn.
		turn_id_t simulatedTurn;
		bool isObserver;
		int playerID;
		bool isOOS = false;
		// Time and duration (in seconds) of the stalls caused by this client, oldest first.
		std::deque<std::pair<double, double>> stalls;
	};

	std::unordered_map<int, Client> m_ClientsData;

	// Cached value - is any client OOS? This is reset when the OOS client leaves.
	bool m_HasSyncError = false;

	// Map of turn -> {Client ID -> state hash}; old indexes <= min(m_ClientsSimulated) are deleted
	std::map<turn_id_t, std::map<int, std::string>> m_ClientStateHashes;

	/// The latest turn for which we have received all commands from all clients
	turn_id_t m_ReadyTurn;

	// Current turn length
	std::uint32_t m_TurnLength;

	// Turn lengths for all previously executed turns
	std::vector<std::uint32_t> m_SavedTurnLengths;

	// Game speed the clients run turns at.
	const float m_GameSpeed;

	// Time of the latest turn release.
	std::optional<double> m_LastReleaseTime;

	// Seconds the turn releases are behind the expected pace, negative if ahead.
	double m_ReleaseLateness{0.0};

	// Whether the game was paused or unpaused since the latest turn release.
	bool m_PausedSinceRelease{false};

	// Reused by SnapshotStats to avoid reallocating every snapshot.
	std::vector<CNetTurnStatsTable::ClientRow> m_StatsRows;

	CNetServerWorker& m_NetServer;
};

#endif // INCLUDED_NETSERVERTURNMANAGER
