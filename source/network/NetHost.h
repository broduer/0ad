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

/**
 * @file
 * Various declarations shared by networking code.
 */

#ifndef NETHOST_H
#define NETHOST_H

#include "lib/external_libraries/enet.h"
#include "ps/CStr.h"

#include <map>
#include <vector>

class CNetMessage;

typedef struct _ENetPeer ENetPeer;
typedef struct _ENetPacket ENetPacket;
typedef struct _ENetHost ENetHost;

struct PlayerAssignment
{
	/**
	 * Whether the player is currently connected and active.
	 * (We retain information on disconnected players to support rejoining,
	 * but don't transmit these to other clients.)
	 */
	bool m_Enabled;

	/// Player name
	CStrW m_Name;

	/// The player that the given host controls, or -1 if none (observer)
	std::int32_t m_PlayerID;

	/// Status - Ready or not: 0 for not ready, 1 for ready, 2 to stay ready
	std::uint8_t m_Status;
};

typedef std::map<CStr, PlayerAssignment> PlayerAssignmentMap; // map from GUID -> assignment

/**
 * Reasons sent by server to clients in disconnection messages.
 * Must be kept in sync with binaries/data/mods/public/gui/common/network.js
 * To avoid ambiguity, use a distinct reason for each callstack leading to a disconnect.
 * NDR_UNKNOWN should remain reserved to the case where it is actually not a known disconnect by the server.
 */
enum NetDisconnectReason
{
	NDR_UNKNOWN = 0,
	NDR_CONNECTION_REQUEST_TIMED_OUT,
	NDR_SERVER_SHUTDOWN,
	NDR_INCORRECT_PROTOCOL_VERSION,
	NDR_SERVER_LOADING,
	NDR_SERVER_ALREADY_IN_GAME,
	NDR_KICKED,
	NDR_BANNED,
	NDR_PLAYERNAME_IN_USE,
	NDR_SERVER_FULL,
	NDR_LOBBY_AUTH_FAILED,
	NDR_GUID_FAILED,
	NDR_INCORRECT_READY_TURN_COMMANDS,
	NDR_INCORRECT_READY_TURN_SIMULATED,
	NDR_SERVER_REFUSED,
	NDR_STUN_PORT_FAILED,
	NDR_STUN_ENDPOINT_FAILED,
	NDR_INCORRECT_SOFTWARE_VERSION
};

struct DestroyHost
{
	void operator()(ENetHost* host) const noexcept
	{
		enet_host_destroy(host);
	}
};

struct DestroyPeer
{
	void operator()(ENetPeer* peer) const noexcept
	{
		// Disconnect immediately (we can't wait for acks)
		enet_peer_disconnect_now(peer, NDR_SERVER_SHUTDOWN);
	}
};

class CNetHost
{
public:
	static const int DEFAULT_CHANNEL = 0;

	/**
	 * Serialise the given message.
	 */
	static std::vector<std::uint8_t> CreatePacket(const CNetMessage* message);

	/**
	 * Initialize ENet.
	 * This must be called before any other networking code.
	 */
	static void Initialize();

	/**
	 * Deinitialize ENet.
	 */
	static void Deinitialize();
};
#endif	// NETHOST_H
