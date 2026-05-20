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

#ifndef NET_SERVER_SESSION_H
#define NET_SERVER_SESSION_H

#include "network/FSM.h"
#include "network/NetFileTransfer.h"
#include "network/NetHost.h"
#include "network/Stream.h"
#include "ps/CStr.h"

#include <deque>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <optional>
#include <span>

#include <sys/poll.h>
#include <sys/socket.h>

#include "ps/CLogger.h"

class CNetServerWorker;
class CNetServerSessionImpl;

/**
 * The server's end of a network session.
 * Represents an abstraction of the state of the client, storing all the per-client data
 * needed by the server.
 *
 * Thread-safety:
 * - This is constructed and used by CNetServerWorker in the network server thread.
 */
class CNetServerSession : public CFsm<CNetServerSession, CNetMessage*>
{
	NONCOPYABLE(CNetServerSession);

public:
	CNetServerSession(CNetServerWorker& server, const int socketFd, const ngtcp2_settings& settings,
		gnutls_certificate_credentials_t credentials, const ngtcp2_pkt_hd& header,
		const ngtcp2_cid& newScid, const ngtcp2_path& path);

	~CNetServerSession();

	CNetServerWorker& GetServer() { return m_Server; }

	const CStr& GetGUID() const { return m_GUID; }
	void SetGUID(const CStr& guid) { m_GUID = guid; }

	const CStrW& GetUserName() const { return m_UserName; }
	void SetUserName(const CStrW& name) { m_UserName = name; }

	std::uint32_t GetHostID() const { return m_HostID; }
	void SetHostID(std::uint32_t id) { m_HostID = id; }

	std::array<std::uint8_t, 16> GetIPAddress() const;

	/**
	 * Number of milliseconds since the latest packet of that client was received.
	 */
	std::uint32_t GetLastReceivedTime() const;

	/**
	 * Average round trip time to the client.
	 */
	std::uint32_t GetMeanRTT() const;

	/**
	 * Sends a disconnection notification to the client,
	 * and sends a NMT_CONNECTION_LOST message to the session FSM.
	 * The server will receive a disconnection notification after a while.
	 * The server will not receive any further messages sent via this session.
	 */
	void Disconnect(NetDisconnectReason reason);

	/**
	 * Send a message to the client.
	 */
	bool SendMessage(const CNetMessage* message);

	CNetFileTransferer& GetFileTransferer() { return m_FileTransferer; }

	void Read(const ngtcp2_addr remote, const std::span<std::uint8_t> data);
	void TrySend(const bool ready, const bool readyToSend, const int socketDescriptor);

	std::vector<ngtcp2_cid> GetAllCids() const;

	struct ProfileData
	{
		ngtcp2_tstamp expiry;
		std::size_t maxPayloadSize;
		ngtcp2_conn_info connectionInfo;
		std::chrono::steady_clock::time_point lastSendTime;
		std::chrono::steady_clock::time_point lastReceiveTime;
	};
	ProfileData GetProfileData() const;

private:
	CNetServerWorker& m_Server;

	const std::unique_ptr<CNetServerSessionImpl> m_Impl;

	CNetFileTransferer m_FileTransferer;

	CStr m_GUID;
	CStrW m_UserName;
	std::uint32_t m_HostID{0};
	CStr m_Password;
	int m_SocketDescriptor;
};

#endif // NET_SERVER_SESSION_H
