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

#include "NetClientSession.h"

#include "lib/code_generation.h"
#include "lib/debug.h"
#include "network/NetClient.h"
#include "network/NetEnet.h"
#include "network/NetMessage.h"
#include "network/NetStats.h"
#include "ps/CLogger.h"
#include "ps/ProfileViewer.h"

#include <cstddef>

constexpr int NETCLIENT_POLL_TIMEOUT = 50;

constexpr int CHANNEL_COUNT = 1;

CNetClientSession::CNetClientSession(CNetClient& client) :
	m_Client(client), m_FileTransferer(*this)
{
}

CNetClientSession::~CNetClientSession()
{
	ENSURE(!m_LoopRunning);
}

bool CNetClientSession::Connect(const CStr& server, const u16 port, ENetHost* enetClient)
{
	ENSURE(!m_LoopRunning);
	ENSURE(!m_Host);
	ENSURE(!m_Server);

	// Create ENet host if necessary.
	m_Host.reset(enetClient != nullptr ? enetClient : PS::Enet::CreateHost(nullptr, 1, CHANNEL_COUNT));

	if (!m_Host)
		return false;

	// Bind to specified host
	ENetAddress addr;
	addr.port = port;
	if (enet_address_set_host(&addr, server.c_str()) < 0)
		return false;

	// Initiate connection to server
	m_Server.reset(enet_host_connect(m_Host.get(), &addr, CHANNEL_COUNT, 0));
	if (!m_Server)
		return false;


	m_Stats = std::make_unique<CNetStatsTable>(*m_Server);
	if (CProfileViewer::IsInitialised())
		g_ProfileViewer.AddRootTable(m_Stats.get());

	return true;
}

void CNetClientSession::RunNetLoop(CNetClientSession* session)
{
	ENSURE(!session->m_LoopRunning);
	session->m_LoopRunning = true;

	debug_SetThreadName("NetClientSession loop");

	while (!session->m_ShouldShutdown)
	{
		ENSURE(session->m_Host && session->m_Server);

		session->m_FileTransferer.Poll();
		session->Poll();
		session->Flush();

		session->m_LastReceivedTime = enet_time_get() - session->m_Server->lastReceiveTime;
		session->m_MeanRTT = session->m_Server->roundTripTime;
	}

	session->m_LoopRunning = false;

	// Deleting the session is handled in this thread as it might outlive the CNetClient.
	SAFE_DELETE(session);
}

void CNetClientSession::Shutdown()
{
	m_ShouldShutdown = true;
}

void CNetClientSession::Poll()
{
	ENetEvent event;

	// Use the timeout to make the thread wait and save CPU time.
	if (enet_host_service(m_Host.get(), &event, NETCLIENT_POLL_TIMEOUT) <= 0)
		return;

	if (event.type == ENET_EVENT_TYPE_CONNECT)
	{
		ENSURE(event.peer == m_Server.get());

		// Report the server address immediately.
		char hostname[256] = "(error)";
		enet_address_get_host_ip(&event.peer->address, hostname, ARRAY_SIZE(hostname));
		LOGMESSAGE("Net client: Connected to %s:%u", hostname, (unsigned int)event.peer->address.port);
		m_Connected = true;
		m_WasConnected = true;

		m_IncomingMessages.push(event);
	}
	else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
	{
		ENSURE(event.peer == m_Server.get());

		// Report immediately.
		LOGMESSAGE("Net client: Disconnected");
		m_Connected = false;

		m_IncomingMessages.push(event);
	}
	else if (event.type == ENET_EVENT_TYPE_RECEIVE)
		m_IncomingMessages.push(event);
}

void CNetClientSession::Flush()
{
	ENetPacket* packet;
	while (m_OutgoingMessages.pop(packet))
		if (enet_peer_send(m_Server.get(), CNetHost::DEFAULT_CHANNEL, packet) < 0)
		{
			// Report the error, but do so silently if we know we are disconnected.
			if (m_Connected)
				LOGERROR("NetClient: Failed to send packet to server");
			else
				LOGMESSAGE("NetClient: Failed to send packet to server");
		}

	enet_host_flush(m_Host.get());
}

void CNetClientSession::ProcessPolledMessages()
{
	ENetEvent event;
	while(m_IncomingMessages.pop(event))
	{
		if (event.type == ENET_EVENT_TYPE_CONNECT)
			m_Client.HandleConnect();
		else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
		{
			// This deletes the session, so we must break;
			if (event.data == 0 && !m_WasConnected)
				m_Client.HandleDisconnect(NDR_CONNECTION_REQUEST_TIMED_OUT);
			else
				m_Client.HandleDisconnect(event.data);
			break;
		}
		else if (event.type == ENET_EVENT_TYPE_RECEIVE)
		{
			CNetMessage* msg = CNetMessageFactory::CreateMessage(event.packet->data, event.packet->dataLength, m_Client.GetScriptInterface());
			if (msg)
			{
				LOGMESSAGE("Net client: Received message %s of size %lu from server", msg->ToString().c_str(), (unsigned long)msg->GetSerializedLength());

				m_Client.HandleMessage(msg);
			}
			// Thread-safe
			enet_packet_destroy(event.packet);
		}
	}
}

bool CNetClientSession::SendMessage(const CNetMessage* message)
{
	ENSURE(m_Host && m_Server);

	// Thread-safe.
	ENetPacket* packet = CNetHost::CreatePacket(message);
	if (!packet)
		return false;

	if (!m_OutgoingMessages.push(packet))
	{
		LOGERROR("NetClient: Failed to push message on the outgoing queue.");
		return false;
	}

	return true;
}

u32 CNetClientSession::GetLastReceivedTime() const
{
	if (!m_Server)
		return 0;

	return m_LastReceivedTime;
}

u32 CNetClientSession::GetMeanRTT() const
{
	if (!m_Server)
		return 0;

	return m_MeanRTT;
}
