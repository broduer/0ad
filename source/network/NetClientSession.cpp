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
#include "network/NetMessage.h"
#include "network/NetProtocol.h"
#include "network/NetStats.h"
#include "network/Stream.h"
#include "ps/CLogger.h"
#include "ps/ProfileViewer.h"

#include <cstddef>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>
#include <netdb.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <poll.h>
#include <sys/socket.h>
#include <variant>

constexpr int NETCLIENT_POLL_TIMEOUT = 50;

struct CNetClientSessionImpl
{
	AddressStorage localAddress;
	std::unique_ptr<gnutls_certificate_credentials_st, CredentialsDeleter> credentials;
	std::unique_ptr<gnutls_session_int, SessionDeleter> session;
	std::unique_ptr<ngtcp2_conn, ConnectionDeleter> quicConnection;
	ngtcp2_crypto_conn_ref connectionReference;
	int socketDescriptor;

	std::chrono::steady_clock::time_point lastSendTime{std::chrono::steady_clock::now()};
	std::chrono::steady_clock::time_point lastReceiveTime{std::chrono::steady_clock::now()};

	std::optional<Stream> streams;

	// Net messages received and waiting for fetching.
	struct ConnectionEstablished{};
	struct Disconnect
	{
		uint64_t errorCode;
	};
	using IncommingMessage = std::variant<ConnectionEstablished, std::vector<std::uint8_t>*, Disconnect>;
	boost::lockfree::queue<IncommingMessage> m_IncomingMessages{16};
};

namespace
{
AddressStorage ConnectSocket(const int fd, const AddressStorage& remoteAddress)
{
	if (connect(fd, &remoteAddress.address.sa, remoteAddress.length))
		throw std::system_error{errno, std::generic_category(), "connect"};

	const int error{fcntl(fd, F_SETFL, O_NONBLOCK)};
	if (error == -1)
		throw std::system_error{error, std::generic_category(), "setting O_NONBLOCK"};

	ngtcp2_sockaddr_union localAddress;
	ngtcp2_socklen localAddressLength{sizeof(localAddress)};
	if (getsockname(fd, &localAddress.sa, &localAddressLength) == -1)
		throw std::runtime_error{fmt::format("getsockname: {}", std::strerror(errno))};

	return {localAddress, localAddressLength};
}

void ClientGnutlsInit(CNetClientSessionImpl& impl)
{
	gnutls_certificate_credentials_t tempCred;
	const int allocRet{gnutls_certificate_allocate_credentials(&tempCred)};
	if (allocRet)
	{
		throw std::runtime_error{fmt::format("cred init failed: {}: {}", allocRet,
			gnutls_strerror(allocRet))};
	}
	impl.credentials.reset(tempCred);

	impl.session = CreateTlsSession(GNUTLS_CLIENT, impl.credentials.get(),
		ngtcp2_crypto_gnutls_configure_client_session, &impl.connectionReference);
}

void ClientRead(CNetClientSessionImpl& impl)
{
	ngtcp2_pkt_info pi{};
	try
	{
		while (true)
		{
			std::array<std::uint8_t, MAX_UDP_PAYLOAD_SIZE> buf;
			AddressStorage remoteAddress;
			const std::size_t nread{ReceiveDatagram(impl.socketDescriptor, buf, remoteAddress)};
			impl.lastReceiveTime = std::chrono::steady_clock::now();

			ngtcp2_path path{
				.local{
					.addr{&impl.localAddress.address.sa},
					.addrlen{impl.localAddress.length}
				},
				.remote{
					.addr{&remoteAddress.address.sa},
					.addrlen{remoteAddress.length}
				}
			};

			const int rv{ngtcp2_conn_read_pkt(impl.quicConnection.get(), &path, &pi, buf.data(),
				static_cast<std::size_t>(nread), Timestamp())};
			if (rv != 0)
				throw std::runtime_error{fmt::format("ngtcp2_conn_read_pkt: {}",
					ngtcp2_strerror(rv))};
			}
		}
	catch (const std::system_error& e)
	{
		if (e.code() != std::error_code{EAGAIN, std::generic_category()} &&
			e.code() != std::error_code{EWOULDBLOCK, std::generic_category()})
		{
			LOGERROR("Receiving datagram: %s", std::strerror(errno));
		}
	}
}
} // anonymous namespace

CNetClientSession::CNetClientSession(CNetClient& client) :
	m_Client(client), m_FileTransferer(*this),
	m_Impl(std::make_unique<CNetClientSessionImpl>())
{
}

CNetClientSession::~CNetClientSession()
{
	ENSURE(!m_LoopRunning);

	constexpr ngtcp2_ccerr reason{
		.type{NGTCP2_CCERR_TYPE_TRANSPORT},
		.error_code{0}
	};

	std::array<std::uint8_t, MAX_UDP_PAYLOAD_SIZE> buffer;
	if (m_Impl->quicConnection)
	{
		const ngtcp2_ssize amount{ngtcp2_conn_write_connection_close(m_Impl->quicConnection.get(),
			nullptr, nullptr, buffer.data(), buffer.size(), &reason, Timestamp())};
		if (amount <= 0)
			LOGERROR("closing connection %s", ngtcp2_strerror(amount));
		else
		{
			try
			{
				SendDatagram(m_Impl->socketDescriptor,
					{buffer.data(), static_cast<std::size_t>(amount)}, {});
			}
			catch (const std::exception& e)
			{
				LOGERROR("Shutting down client: %s", e.what());
			}
		}
	}

	close(m_Impl->socketDescriptor);
}

bool CNetClientSession::Connect(const CStr& server, const std::uint16_t port)
{
	ENSURE(!m_LoopRunning);

	const auto [socketDescriptor, remoteAddress] = CreateSocket(server.c_str(), port,
		addrinfo{ .ai_family{AF_UNSPEC} },
		*[](int, const addrinfo&){ return true; });
	m_Impl->socketDescriptor = socketDescriptor;

	m_Impl->localAddress = ConnectSocket(socketDescriptor, remoteAddress);

	ClientGnutlsInit(*m_Impl);

	const ngtcp2_path path{
		.local{
			.addr{const_cast<ngtcp2_sockaddr*>(&m_Impl->localAddress.address.sa)},
			.addrlen{m_Impl->localAddress.length},
		},
		.remote{
			.addr{const_cast<ngtcp2_sockaddr*>(&remoteAddress.address.sa)},
			.addrlen{remoteAddress.length},
		}
	};
	ngtcp2_callbacks callbacks{InitCallbacks()};
	callbacks.client_initial = &ngtcp2_crypto_client_initial_cb;
	callbacks.recv_stream_data = [](ngtcp2_conn* conn, const std::uint32_t /*flags*/,
		const std::int64_t streamId, const std::size_t /*offset*/, const std::uint8_t* data,
		std::size_t dataSize, void* userData, void*)
	{
		CNetClientSessionImpl& impl = *static_cast<CNetClientSessionImpl*>(userData);
		impl.streams.value().Receive({data, dataSize});
		for (auto message : impl.streams.value().GetAllMessages())
			impl.m_IncomingMessages.push(new std::vector<std::uint8_t>{std::move(message)});
		IncreaseWindow(conn, streamId, dataSize);
		return 0;
	};
	callbacks.stream_open = [](ngtcp2_conn*, const std::int64_t streamId, void* userData)
	{
		CNetClientSessionImpl& impl{*static_cast<CNetClientSessionImpl*>(userData)};
		impl.streams.emplace(streamId);
		impl.m_IncomingMessages.push(CNetClientSessionImpl::ConnectionEstablished{});
		return 0;
	};

	ngtcp2_cid dcid{GenerateRandomCid()};
	ngtcp2_cid scid{GenerateRandomCid()};
	ngtcp2_settings settings{InitSettings()};

	ngtcp2_transport_params params{InitTransportParams()};
	params.initial_max_stream_data_bidi_remote = 128 * KiB;
	params.initial_max_streams_bidi = 1;

	ngtcp2_conn* tempConn;
	if (const int rv{ngtcp2_conn_client_new(&tempConn, &dcid, &scid, &path,
		NGTCP2_PROTO_VER_V1, &callbacks, &settings, &params, nullptr, m_Impl.get())})
	{
		throw std::runtime_error{fmt::format("ngtcp2_conn_client_new: {}", ngtcp2_strerror(rv))};
	}
	ngtcp2_conn_set_keep_alive_timeout(tempConn, KEEPALIVE_TIME);
	m_Impl->quicConnection.reset(tempConn);

	ngtcp2_conn_set_tls_native_handle(m_Impl->quicConnection.get(),
		m_Impl->session.get());

	m_Impl->connectionReference.user_data = m_Impl.get();
	m_Impl->connectionReference.get_conn = [](ngtcp2_crypto_conn_ref* conn_ref)
	{
		CNetClientSessionImpl* c{static_cast<CNetClientSessionImpl*>(conn_ref->user_data)};
		return c->quicConnection.get();
	};

	WriteStream(m_Impl->socketDescriptor, *m_Impl->quicConnection,
		m_Impl->streams.has_value() ? &m_Impl->streams.value() : nullptr, {},
		m_Impl->lastSendTime);

	m_Stats = std::make_unique<CNetStatsTable>(*this);
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
		// ENSURE(session->m_Host && session->m_Server);

		session->m_FileTransferer.Poll();
		try
		{
			session->Poll();
		}
		catch (const std::runtime_error&)
		{
			// Report immediately.
			LOGMESSAGE("Net client: Disconnected");
			session->m_Impl->m_IncomingMessages.push(CNetClientSessionImpl::Disconnect{
				ngtcp2_conn_get_ccerr(session->m_Impl->quicConnection.get())->error_code});
			return;
		}
		session->Flush();

		ngtcp2_conn_info connectionInfo;
		ngtcp2_conn_get_conn_info(session->m_Impl->quicConnection.get(), &connectionInfo);

		session->m_LastReceivedTime =
			std::chrono::steady_clock::now() - session->m_Impl->lastReceiveTime;
		session->m_MeanRTT = connectionInfo.smoothed_rtt;

		const std::lock_guard _{session->m_ProfileDataMutex};
		session->m_ProfileData.expiry = ngtcp2_conn_get_expiry(session->m_Impl->quicConnection.get());
		session->m_ProfileData.maxPayloadSize = ngtcp2_conn_get_path_max_tx_udp_payload_size(
			session->m_Impl->quicConnection.get());
		session->m_ProfileData.lastSendTime = session->m_Impl->lastSendTime;
		session->m_ProfileData.lastReceiveTime = session->m_Impl->lastReceiveTime;
		session->m_ProfileData.connectionInfo = connectionInfo;
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
	pollfd pollFileDescriptor{
		.fd{m_Impl->socketDescriptor},
		.events{POLLIN}
	};

	const int result{poll(&pollFileDescriptor, 1, NETCLIENT_POLL_TIMEOUT)};
	if (result < 0)
	{
		LOGERROR("Error while waiting for poll: %s", std::strerror(errno));
		return;
	}
	if (result == 0)
	{
		if (const int rv = ngtcp2_conn_handle_expiry(m_Impl->quicConnection.get(), Timestamp()))
		{
			throw std::runtime_error{fmt::format("ngtcp2_conn_handle_expiry: {}",
				ngtcp2_strerror(rv))};
		}
	}
	else
		ClientRead(*m_Impl);

	WriteStream(m_Impl->socketDescriptor, *m_Impl->quicConnection,
		m_Impl->streams.has_value() ? &m_Impl->streams.value() : nullptr, {},
		m_Impl->lastSendTime);
}

void CNetClientSession::Flush()
{
	std::vector<std::uint8_t>* message;
	while (m_OutgoingMessages.pop(message))
	{
		std::unique_ptr<std::vector<std::uint8_t>> data{message};
		if (m_Impl->streams.has_value())
			m_Impl->streams.value().PushData(std::move(*data));
		else
			LOGERROR("no stream to send message");
	}
}

void CNetClientSession::ProcessPolledMessages()
{
	CNetClientSessionImpl::IncommingMessage query{};
	bool shutdown{false};
	while(!shutdown && m_Impl->m_IncomingMessages.pop(query))
	{
		std::visit([&]<typename Message>(Message message)
		{
			if constexpr (std::same_as<Message, CNetClientSessionImpl::ConnectionEstablished>)
			{
				m_Client.HandleConnect();
			}
			else if constexpr (std::same_as<Message, CNetClientSessionImpl::Disconnect>)
			{
				m_Client.HandleDisconnect(message.errorCode);
				shutdown = true;
			}
			else
			{
				static_assert(std::same_as<Message, std::vector<std::uint8_t>*>);
				std::unique_ptr<std::vector<std::uint8_t>> data{message};
				CNetMessage* msg = CNetMessageFactory::CreateMessage(*data, m_Client.GetScriptInterface());
				if (msg)
				{
					LOGMESSAGE("Net client: Received message {} of size {} from server",
						msg->ToString(), msg->GetSerializedLength());

					m_Client.HandleMessage(msg);
				}
			}
		}, query);
	}
}

bool CNetClientSession::SendMessage(const CNetMessage* message)
{
	// ENSURE(m_Host && m_Server);

	if (!m_OutgoingMessages.push(new std::vector{CNetHost::CreatePacket(message)}))
	{
		LOGERROR("NetClient: Failed to push message on the outgoing queue.");
		return false;
	}

	return true;
}

std::uint32_t CNetClientSession::GetLastReceivedTime() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(m_LastReceivedTime.load()).count();
}

std::uint32_t CNetClientSession::GetMeanRTT() const
{
	return m_MeanRTT / 1000000;
}

CNetClientSession::ProfileData CNetClientSession::GetProfileData() const
{
	const std::lock_guard _{m_ProfileDataMutex};
	return m_ProfileData;
}

