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

#ifndef NETSERVER_H
#define NETSERVER_H

#include "lib/code_annotation.h"
#include "lib/config2.h"
#include "network/NetHost.h"
#include "ps/CStr.h"

#include <ctime>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class CNetMessage;
class CNetServerSession;
class CNetServerTurnManager;
class CNetStatsTable;
class CPlayerAssignmentMessage;
class CSimulationMessage;
namespace Script { class Interface; }
namespace Script { class Request; }
struct CreateSocketResult;
template <typename MessageType> class CFsmEvent;

enum NetServerState
{
	// The server is open and accepting connections. This is the screen where
	// rules are set up by the operator and where players join and select civs
	// and stuff.
	SERVER_STATE_PREGAME,

	// All the hosts are connected and are loading the game
	SERVER_STATE_LOADING,

	// The one with all the killing ;-)
	SERVER_STATE_INGAME,

	// The game is over and someone has won. Players might linger to chat or
	// download the replay log.
	SERVER_STATE_POSTGAME
};

/**
 * Server session representation of client state
 */
enum NetServerSessionState
{
	// The client has disconnected or been disconnected
	NSS_UNCONNECTED,

	// The client has just connected and we're waiting for its handshake message,
	// to agree on the protocol version
	NSS_HANDSHAKE,

	// The client has handshook and we're waiting for its lobby authentication message
	NSS_LOBBY_AUTHENTICATE,

	// The client has handshook and we're waiting for its authentication message,
	// to find its name and check its password etc
	NSS_AUTHENTICATE,

	// The client has fully joined, and is in the pregame setup stage
	// or is loading the game.
	// Server must be in SERVER_STATE_PREGAME or SERVER_STATE_LOADING.
	NSS_PREGAME,

	// The client has authenticated but the game was already started,
	// so it's synchronising with the game state from other clients
	NSS_JOIN_SYNCING,

	// The client is running the game.
	// Server must be in SERVER_STATE_LOADING or SERVER_STATE_INGAME.
	NSS_INGAME
};

/**
 * Network server worker thread.
 * (This is run in a thread so that client/server communication is not delayed
 * by the host player's framerate - the only delay should be the network latency.)
 *
 * Thread-safety:
 * - Constructor/destructor must be called from the main thread.
 * - The main thread may push commands onto the Queue members,
 *   while holding the m_WorkerMutex lock.
 * - Public functions (SendMessage, Broadcast) must be called from the network
 *   server thread.
 */
class CNetServerWorker
{
	NONCOPYABLE(CNetServerWorker);

public:
	// Public functions for CNetSession/CNetServerTurnManager to use:

	/**
	 * Disconnects a player from gamesetup or session.
	 */
	void KickPlayer(const CStrW& playerName, const bool ban);

	/**
	 * Send a message to all clients who match one of the given states.
	 */
	bool Multicast(const CNetMessage* message, const std::vector<NetServerSessionState>& targetStates,
		const std::optional<std::vector<std::string>>& receivers = std::nullopt);

private:
	friend class CNetServer;

	CNetServerWorker(const bool continuesSavedGame, std::uint16_t port, const bool useLobbyAuth,
		std::string password, std::string controllerSecret, std::string initAttributes);
	~CNetServerWorker();

	bool CheckPassword(const std::string& password, const std::string& salt) const;

	/**
	 * The given GUID will be (re)assigned to the given player ID.
	 * Any player currently using that ID will be unassigned.
	 */
	void AssignPlayer(int playerID, const CStr& guid);

	/**
	 * Switch in game mode. The clients will have to be notified to start the
	 * game. This method is called by StartGame and StartSavedGame
	 */
	void PreStartGame(const CStr& initAttribs);

	/**
	 * Switch in game mode and notify all clients to start the game.
	 */
	void StartGame(const CStr& initAttribs);

	/**
	 * Switch in game mode and notify all clients to start the saved game.
	 */
	void StartSavedGame(const CStr& initAttribs);

	/**
	 * Make a player name 'nicer' by limiting the length and removing forbidden characters etc.
	 */
	static CStrW SanitisePlayerName(const CStrW& original);

	/**
	 * Make a player name unique, if it matches any existing session's name.
	 */
	CStrW DeduplicatePlayerName(const CStrW& original);

public:
	/**
	 * Get the script context used for init attributes.
	 */
	const Script::Interface& GetScriptInterface();
private:

	/**
	 * Set the turn length to a fixed value.
	 * TODO: we should replace this with some adaptive lag-dependent computation.
	 */
	void SetTurnLength(std::uint32_t msecs);

	void ProcessLobbyAuth(const CStr& name, const CStr& token);

	void AddPlayer(const CStr& guid, const CStrW& name);
	void RemovePlayer(const CStr& guid);
	void SendPlayerAssignments();
	void ClearAllPlayerReady();

	void SetupSession(CNetServerSession* session);

	void OnUserJoin(CNetServerSession* session);
	void OnUserLeave(CNetServerSession* session);

	static bool OnClientHandshake(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnAuthenticate(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnSimulationCommand(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnFlare(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnSyncCheck(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnEndCommandBatch(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnChat(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnReady(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnClearAllReady(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnGameSetup(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnAssignPlayer(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnGameStart(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnSavedGameStart(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnLoadedGame(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnJoinSyncingLoadedGame(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnRejoined(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnKickPlayer(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnDisconnect(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);
	static bool OnClientPaused(CNetServerSession* session, CFsmEvent<CNetMessage*>* event);

	/**
	 * Checks if all clients have finished loading.
	 * If so informs the clients about that and change the server state.
	 *
	 * Returns if all clients finished loading.
	 */
	bool CheckGameLoadStatus(CNetServerSession* changedSession);

	void ConstructPlayerAssignmentMessage(CPlayerAssignmentMessage& message);

public:
	void HandleMessageReceive(CNetMessage* message, CNetServerSession* session);
public:

	/**
	 * Send a network warning if the connection to a client is being lost or has bad latency.
	 */
	void CheckClientConnections();

	void SendHolePunchingMessage(const CStr& ip, std::uint16_t port);

	/**
	 * Internal script context for (de)serializing script messages,
	 * and for storing init attributes.
	 * (TODO: we shouldn't bother deserializing (except for debug printing of messages),
	 * we should just forward messages blindly and efficiently.)
	 */
	Script::Interface* m_ScriptInterface{nullptr};

	PlayerAssignmentMap m_PlayerAssignments;

	/**
	 * Stores the most current init attributes.
	 * NB: this is not guaranteed to be up-to-date until the server is LOADING or INGAME.
	 * At that point, the settings are frozen and ought to be identical to the simulation Init Attributes.
	 */
	JS::PersistentRootedValue m_InitAttributes;

	/**
	 * Whether this match continues a saved game.
	 */
	const bool m_ContinuesSavedGame;

	/**
	 * Whether this match requires lobby authentication.
	 */
	const bool m_LobbyAuth;

	class Impl;

	std::vector<std::unique_ptr<CNetServerSession>> m_Sessions;

	std::unique_ptr<CNetStatsTable> m_Stats;

	NetServerState m_State{SERVER_STATE_PREGAME};

	CStrW m_ServerName{L"Unnamed Server"};

	std::vector<std::array<std::uint8_t, 16>> m_BannedIPs;
	std::vector<CStrW> m_BannedPlayers;

	CStr m_Password;

	/**
	 * Holds the GUIDs of all currently paused players.
	 */
	std::vector<CStr> m_PausingPlayers;

	std::uint32_t m_NextHostID{1};

	std::unique_ptr<CNetServerTurnManager> m_ServerTurnManager;

	/**
	 * The GUID of the client in control of the game (the 'host' from the players' perspective).
	 */
	CStr m_ControllerGUID;

	/**
	 * The 'secret' used to identify the controller of the game.
	 */
	std::string m_ControllerSecret;

	/**
	 * A copy of all simulation commands received so far, indexed by
	 * turn number, to simplify support for rejoining etc.
	 * TODO: verify this doesn't use too much RAM.
	 */
	std::vector<std::vector<CSimulationMessage>> m_SavedCommands;

	/**
	 * The latest copy of the simulation state, received from an existing
	 * client when a new client has asked to rejoin the game.
	 */
	std::string m_JoinSyncFile;

	/**
	 * The loaded game data when a game is loaded.
	 */
	 std::string m_SavedState;

	/**
	 *  Time when the clients connections were last checked for timeouts and latency.
	 */
	std::time_t m_LastConnectionCheck{0};

private:
	// Thread-related stuff:

#if CONFIG2_MINIUPNPC
	/**
	 * Try to find a UPnP root on the network and setup port forwarding.
	 */
	static void SetupUPnP(const std::uint16_t port);
	std::thread m_UPnPThread;
#endif

	static void RunThread(CNetServerWorker* data, const std::string& initAttributes,
		const CreateSocketResult& socket);
	void Run(const std::string& initAttributes, const CreateSocketResult& socket);
	bool RunStep(Impl& impl);

	std::thread m_WorkerThread;
	mutable std::mutex m_WorkerMutex;

	int m_SocketDescriptor;
	std::uint16_t m_LocalPort;

	// protected by m_WorkerMutex
	bool m_Shutdown{false};

	// Queues for messages sent by the game thread (protected by m_WorkerMutex):
	std::vector<bool> m_StartGameQueue;
	std::vector<std::pair<CStr, CStr>> m_LobbyAuthQueue;
	std::vector<std::uint32_t> m_TurnLengthQueue;
};

/**
 * Network server interface. Handles all the coordination between players.
 * One person runs this object, and every player (including the host) connects their CNetClient to it.
 *
 * The actual work is performed by CNetServerWorker in a separate thread.
 */
class CNetServer
{
	NONCOPYABLE(CNetServer);
public:
	CNetServer(const bool isSavedGame, std::uint16_t port, const bool useLobbyAuth = false,
		std::string password = {}, std::string controllerSecret = {}, std::string initAttributes = {});

	/**
	 * Call from the GUI to asynchronously notify all clients that they should start loading the game.
	 * SetupConnection must be called at least once.
	 */
	void StartGame();

	/**
	 * Set the turn length to a fixed value.
	 * TODO: we should replace this with some adapative lag-dependent computation.
	 */
	void SetTurnLength(std::uint32_t msecs);

	bool UseLobbyAuth() const;

	void OnLobbyAuth(const CStr& name, const CStr& token);

	void SendHolePunchingMessage(const CStr& ip, std::uint16_t port);

	/**
	 * Return the externally accessible IP.
	 */
	CStr GetPublicIp() const;

	/**
	 * Return the externally accessible port.
	 */
	std::uint16_t GetPublicPort() const;

	/**
	 * Return the serving port on the local machine.
	 */
	std::uint16_t GetLocalPort() const;

	/**
	 * Check if password is valid. If is not, increase number of failed attempts of the lobby user.
	 * This is used without established direct session with the client, to prevent brute force attacks
	 * when guessing password trying to get connection data from the host.
	 * @return true iff password is valid
	 */
	bool CheckPasswordAndIncrement(const std::string& username, const std::string& password, const std::string& salt);

	/**
	 * Check if user reached certain number of failed attempts.
	 * @see m_BanAfterNumberOfTries
	 * @see CheckPasswordAndBan
	 */
	bool IsBanned(const std::string& username) const;

private:
	CNetServerWorker m_Worker;
	const bool m_LobbyAuth;
	std::uint16_t m_PublicPort{20595};
	CStr m_PublicIp;
	CStr m_Password;
	std::unordered_map<std::string, int> m_FailedAttempts;
};

/// Global network server for the standard game
extern CNetServer *g_NetServer;

#endif // NETSERVER_H
