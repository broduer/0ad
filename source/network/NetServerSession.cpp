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

#include "NetServerSession.h"

#include "network/NetMessage.h"
#include "network/NetMessages.h"
#include "network/NetProtocol.h"
#include "network/NetServer.h"
#include "ps/CLogger.h"


#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>

class CNetServerSessionImpl
{
public:
	CNetServerSessionImpl(const ngtcp2_settings& settings,
		gnutls_certificate_credentials_t credentials, const ngtcp2_pkt_hd& header,
		const ngtcp2_cid& scid, const ngtcp2_path& path);

	std::vector<std::vector<std::uint8_t>> Read(const ngtcp2_addr remote,
		const std::span<std::uint8_t> data);
	void Write(const int socketFd);

	std::unique_ptr<gnutls_session_int, SessionDeleter> m_TlsSession;
	ngtcp2_crypto_conn_ref m_ConnRef;
	std::unique_ptr<ngtcp2_conn, ConnectionDeleter> m_QuicConnection;

	// This times are not used in the protocol. They are only shown to the
	// user.
	std::chrono::steady_clock::time_point m_LastSendTime{std::chrono::steady_clock::now()};
	std::atomic<std::chrono::steady_clock::time_point> m_LastReceiveTime{
		std::chrono::steady_clock::now()};
	std::atomic<std::uint32_t> m_MeanRTT{0};
public:
	AddressStorage m_RemoteAddress;
	std::optional<Stream> m_Stream;

	std::vector<std::vector<std::uint8_t>> m_ReceivedMessages;
};

CNetServerSessionImpl::CNetServerSessionImpl(const ngtcp2_settings& settings,
	gnutls_certificate_credentials_t credentials, const ngtcp2_pkt_hd& header,
	const ngtcp2_cid& newScid, const ngtcp2_path& path):
	m_ConnRef{
		.get_conn{[](ngtcp2_crypto_conn_ref* connRef)
		{
			return reinterpret_cast<std::unique_ptr<ngtcp2_conn, ConnectionDeleter>*>(connRef->user_data)->get();
		}},
		.user_data{&m_QuicConnection}
	},
	m_TlsSession{CreateTlsSession(GNUTLS_SERVER, credentials,
		ngtcp2_crypto_gnutls_configure_server_session, &m_ConnRef)}
{
	ngtcp2_callbacks callbacks{InitCallbacks()};
	callbacks.recv_client_initial = &ngtcp2_crypto_recv_client_initial_cb;
	callbacks.recv_stream_data = [](ngtcp2_conn* conn, const std::uint32_t /*flags*/,
		const std::int64_t streamId, const std::uint64_t /*offset*/, const std::uint8_t* data,
		const std::size_t datalen, void* userData, void* /*streamUserData*/)
	{
		CNetServerSessionImpl& connection{*static_cast<CNetServerSessionImpl*>(userData)};
		connection.m_Stream.value().Receive({data, datalen});
		IncreaseWindow(conn, streamId, datalen);
		return 0;
	};
	callbacks.acked_stream_data_offset = [](ngtcp2_conn*, const std::int64_t, const std::uint64_t offset,
		const std::uint64_t dataLength, void* userData, void*)
	{
		CNetServerSessionImpl& connection{*static_cast<CNetServerSessionImpl*>(userData)};
		Stream& stream{connection.m_Stream.value()};
		stream.MarkAcknowledged(offset + dataLength);
		return 0;
	};
	callbacks.recv_tx_key = [](ngtcp2_conn*, const ngtcp2_encryption_level level, void* userData)
	{
		if (level != NGTCP2_ENCRYPTION_LEVEL_1RTT)
			return 0;
		CNetServerSessionImpl& connection{*static_cast<CNetServerSessionImpl*>(userData)};

		std::int64_t streamId;
		if (ngtcp2_conn_open_bidi_stream(connection.m_QuicConnection.get(), &streamId, nullptr))
			return NGTCP2_ERR_CALLBACK_FAILURE;
		connection.m_Stream.emplace(streamId);

		const CSrvHandshakeMessage handshake(CreateHandshake<CSrvHandshakeMessage>());
		connection.m_Stream.value().PushMessage(&handshake);
		return 0;
	};

	ngtcp2_transport_params params{InitTransportParams()};
	params.initial_max_stream_data_bidi_local = 128 * KiB;
	ngtcp2_cid_init(&params.original_dcid, header.dcid.data, header.dcid.datalen);
	params.original_dcid_present = true;

	ngtcp2_conn* tempConn;
	if (const int ret = ngtcp2_conn_server_new(&tempConn, &header.scid, &newScid, &path, header.version,
		&callbacks, &settings, &params, nullptr, this))
	{
		throw std::runtime_error{fmt::format("ngtcp2_conn_server_new: {}",
			ngtcp2_strerror(ret))};
	}
	ngtcp2_conn_set_keep_alive_timeout(tempConn, KEEPALIVE_TIME);
	m_QuicConnection.reset(tempConn);

	memcpy(&m_RemoteAddress.address, path.remote.addr, path.remote.addrlen);
	m_RemoteAddress.length = path.remote.addrlen;

	ngtcp2_conn_set_tls_native_handle(m_QuicConnection.get(), m_TlsSession.get());
}

std::vector<std::vector<std::uint8_t>> CNetServerSessionImpl::Read(const ngtcp2_addr remote,
	const std::span<std::uint8_t> data)
{
	const ngtcp2_path path{
		.local{ngtcp2_conn_get_path(m_QuicConnection.get())->local},
		.remote{remote}
	};

	m_LastReceiveTime = std::chrono::steady_clock::now();

	ngtcp2_pkt_info pi{};
	if (const int ret{ngtcp2_conn_read_pkt(m_QuicConnection.get(), &path, &pi, data.data(), data.size(),
		Timestamp())})
	{
		throw std::runtime_error{"Destroy connection"};
	}

	return m_Stream.value().GetAllMessages();
}

void CNetServerSessionImpl::Write(const int socketFd)
{
	if (ngtcp2_conn_in_closing_period(m_QuicConnection.get()) ||
		ngtcp2_conn_in_draining_period(m_QuicConnection.get()))
	{
		return;
	}

	const ngtcp2_addr remote{const_cast<ngtcp2_sockaddr*>(&m_RemoteAddress.address.sa),
		m_RemoteAddress.length};
	WriteStream(socketFd, *m_QuicConnection, nullptr, remote, m_LastSendTime);

	if (m_Stream.has_value())
	{
		WriteStream(socketFd, *m_QuicConnection, &m_Stream.value(), remote, m_LastSendTime);
	}
	ngtcp2_conn_info connectionInfo;
	ngtcp2_conn_get_conn_info(m_QuicConnection.get(), &connectionInfo);

	m_MeanRTT = connectionInfo.smoothed_rtt;
}

CNetServerSession::CNetServerSession(CNetServerWorker& server, const int socketFd,
	const ngtcp2_settings& settings, gnutls_certificate_credentials_t credentials,
	const ngtcp2_pkt_hd& header, const ngtcp2_cid& newScid, const ngtcp2_path& path) :
	m_Server(server),
	m_Impl{std::make_unique<CNetServerSessionImpl>(settings, credentials, header, newScid, path)},
	m_FileTransferer(*this),
	m_SocketDescriptor{socketFd}
{
}

CNetServerSession::~CNetServerSession() = default;

std::array<std::uint8_t, 16> CNetServerSession::GetIPAddress() const
{
	return std::to_array(m_Impl->m_RemoteAddress.address.in6.sin6_addr.s6_addr);
}

std::uint32_t CNetServerSession::GetLastReceivedTime() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - m_Impl->m_LastReceiveTime.load()).count();
}

std::uint32_t CNetServerSession::GetMeanRTT() const
{
	return m_Impl->m_MeanRTT / 1000000;
}

void CNetServerSession::Disconnect(NetDisconnectReason reason)
{
	if (reason == NDR_UNKNOWN)
		LOGWARNING("Disconnecting client without communicating the disconnect reason!");

	Update(static_cast<unsigned int>(NMT_CONNECTION_LOST), nullptr);
	m_Impl->m_Stream.reset();

	const ngtcp2_ccerr quicReason{
		.type{NGTCP2_CCERR_TYPE_APPLICATION},
		.error_code{reason}
	};

	ngtcp2_sockaddr_union local;
	ngtcp2_sockaddr_union remote;
	ngtcp2_path path{
		.local{.addr{&local.sa}},
		.remote{.addr{&remote.sa}}
	};
	std::array<std::uint8_t, MAX_UDP_PAYLOAD_SIZE> buffer;
	const ngtcp2_ssize amount{ngtcp2_conn_write_connection_close(m_Impl->m_QuicConnection.get(),
		&path, nullptr, buffer.data(), buffer.size(), &quicReason, Timestamp())};
	if (amount < 0)
		LOGERROR("Closing connection {}", ngtcp2_strerror(static_cast<int>(amount)));
	if (amount <= 0)
		return;

	try
	{
		SendDatagram(m_SocketDescriptor, {buffer.data(), static_cast<std::size_t>(amount)},
			path.remote);
	}
	catch (const std::exception& e)
	{
		LOGERROR("Server disconnecting: {}", e.what());
	}
}

bool CNetServerSession::SendMessage(const CNetMessage* message)
{
	m_Impl->m_Stream.value().PushMessage(message);
	return true;
}

void CNetServerSession::Read(const ngtcp2_addr remote, const std::span<std::uint8_t> data)
{
	for (std::vector<std::uint8_t>& messageData : m_Impl->Read(remote, data))
	{
		std::unique_ptr<CNetMessage> message{CNetMessageFactory::CreateMessage(messageData,
			m_Server.GetScriptInterface())};
		m_Server.HandleMessageReceive(message.get(), this);
	}
}

void CNetServerSession::TrySend(const bool ready, const bool readyToSend, const int socketDescriptor)
{
	if (ready)
	{
		const int result{ngtcp2_conn_handle_expiry(m_Impl->m_QuicConnection.get(),
			Timestamp())};
		if (result < 0)
		{
			LOGERROR("ngtcp2_conn_handle_expiry: {}", ngtcp2_strerror(result));
			return;
		}
	}
	if (ready || readyToSend)
		m_Impl->Write(socketDescriptor);
}

std::vector<ngtcp2_cid> CNetServerSession::GetAllCids() const
{
	const std::size_t amount{ngtcp2_conn_get_scid(m_Impl->m_QuicConnection.get(), nullptr)};
	std::vector<ngtcp2_cid> cids(amount);
	ngtcp2_conn_get_scid(m_Impl->m_QuicConnection.get(), cids.data());
	return cids;
}

CNetServerSession::ProfileData CNetServerSession::GetProfileData() const
{
	ngtcp2_conn_info info;
	ngtcp2_conn_get_conn_info(m_Impl->m_QuicConnection.get(), &info);
	return {
		.expiry{ngtcp2_conn_get_expiry(m_Impl->m_QuicConnection.get())},
		.maxPayloadSize{ngtcp2_conn_get_path_max_tx_udp_payload_size(
			m_Impl->m_QuicConnection.get())},
		.connectionInfo{info},
		.lastSendTime{m_Impl->m_LastSendTime.time_since_epoch()},
		.lastReceiveTime{m_Impl->m_LastReceiveTime.load().time_since_epoch()}
	};
}
