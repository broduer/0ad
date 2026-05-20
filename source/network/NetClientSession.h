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

#ifndef NETSESSION_H
#define NETSESSION_H

#include "lib/code_annotation.h"
#include "network/NetFileTransfer.h"
#include "network/NetHost.h"

#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <ngtcp2/ngtcp2.h>

class CNetClient;
class CNetMessage;
class CNetStatsTable;
class CStr;
struct CNetClientSessionImpl;

/**
 * @file
 * Network client/server sessions.
 *
 * Each session has two classes: CNetClientSession runs on the client,
 * and CNetServerSession runs on the server.
 * A client runs one session at once; a server typically runs many.
 */

/**
 * The client end of a network session.
 * Provides an abstraction of the network interface, allowing communication with the server.
 * The NetClientSession is threaded, so all calls to the public interface must be thread-safe.
 */
class CNetClientSession
{
	NONCOPYABLE(CNetClientSession);

public:
	CNetClientSession(CNetClient& client);
	~CNetClientSession();

	bool Connect(const CStr& server, const std::uint16_t port);

	/**
	 * The client NetSession is threaded to avoid getting timeouts if the main thread hangs.
	 * Call Connect() before starting this loop.
	 */
	static void RunNetLoop(CNetClientSession* session);

	/**
	 * Shut down the net session.
	 */
	void Shutdown();

	/**
	 * Processes pending messages.
	 */
	void ProcessPolledMessages();

	/**
	 * Queue up a message to send to the server on the next Loop() call.
	 */
	bool SendMessage(const CNetMessage* message);

	/**
	 * Number of milliseconds since the most recent packet of the server was received.
	 */
	std::uint32_t GetLastReceivedTime() const;

	/**
	 * Average round trip time to the server.
	 */
	std::uint32_t GetMeanRTT() const;

	CNetFileTransferer& GetFileTransferer() { return m_FileTransferer; }

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
	/**
	 * Process queued incoming messages.
	 */
	void Poll();

	/**
	 * Flush queued outgoing network messages.
	 */
	void Flush();

	CNetClient& m_Client;

	CNetFileTransferer m_FileTransferer;

	// Net messages to send on the next flush() call.
	boost::lockfree::queue<std::vector<std::uint8_t>*> m_OutgoingMessages{16};

	// Wrapper around stats - those are atomic as the code is lock-free.
	std::atomic<std::chrono::steady_clock::duration> m_LastReceivedTime{
		std::chrono::steady_clock::duration::zero()};
	std::atomic<std::uint32_t> m_MeanRTT{0};
	mutable std::mutex m_ProfileDataMutex;
	ProfileData m_ProfileData;

	// If this is true, calling Connect() or deleting the session is an error.
	std::atomic<bool> m_LoopRunning{false};
	std::atomic<bool> m_ShouldShutdown{false};

	std::unique_ptr<CNetStatsTable> m_Stats;

	const std::unique_ptr<CNetClientSessionImpl> m_Impl;
};

#endif	// NETSESSION_H
