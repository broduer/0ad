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

#include "NetProtocol.h"

#include "ps/CLogger.h"
#include "ps/CStr.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <gnutls/crypto.h>
#include <netdb.h>
#include <ngtcp2/ngtcp2_crypto.h>

namespace
{
constexpr bool ENABLE_NGTCP2_LOGGING{false};
constexpr const char* TLS_PRIORITY{
	"PERFORMANCE:-VERS-DTLS-ALL:-VERS-TLS1.0:-VERS-TLS1.1:-VERS-TLS1.2:-SHA1:-AES-128-CBC:-AES-256-CBC:"
	"-SIGN-RSA-SHA1:-SIGN-ECDSA-SHA1:%DISABLE_TLS13_COMPAT_MODE"};

template <typename ModType>
std::string GetModName(const ModType& mod)
{
	return mod.m_Name + "-" + mod.m_Version;
}
}

CreateSocketResult CreateSocket(const char* host, const std::uint16_t port, const addrinfo& hints,
	const SocketTest& socketTest)
{
	addrinfo setHints = hints;
	setHints.ai_socktype = SOCK_DGRAM;
	setHints.ai_protocol = IPPROTO_UDP;

	addrinfo* res;
	const int rv{getaddrinfo(host, std::to_string(port).c_str(), &setHints, &res)};
	if (rv)
		throw std::runtime_error{fmt::format("getaddrinfo: {}", gai_strerror(rv))};
	std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> infoList{res, &freeaddrinfo};

	addrinfo* rp;
	int fd{-1};
	for (rp = res; rp; rp = rp->ai_next)
	{
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd == -1)
			continue;

		try
		{
			if (!socketTest(fd, *rp))
			{
				close(std::exchange(fd, -1));
				continue;
			}
		}
		catch(...)
		{
			close(fd);
			throw;
		}
		break;
	}

	if (fd == -1)
		throw std::runtime_error{"unable to create a socket"};

	CreateSocketResult result{
		.descriptor{fd},
		.address{.length{rp->ai_addrlen}}
	};
	if (rp->ai_family == AF_INET6)
		result.address.address.in6 = reinterpret_cast<ngtcp2_sockaddr_in6&>(*rp->ai_addr);
	else
		result.address.address.in = reinterpret_cast<ngtcp2_sockaddr_in&>(*rp->ai_addr);

	return result;
}

std::size_t ReceiveDatagram(const int fd, const std::span<std::uint8_t> data,
	AddressStorage& remoteAddress)
{
	iovec iov{
		.iov_base{data.data()},
		.iov_len{data.size()}
	};

	msghdr msg{};
	msg.msg_name = &remoteAddress.address.sa;
	msg.msg_namelen = remoteAddress.length;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	ssize_t ret;
	do
		ret = recvmsg(fd, &msg, 0);
	while (ret < 0 && errno == EINTR);
	if (ret < 0)
		throw std::system_error{errno, std::generic_category(), "recvmsg"};

	remoteAddress.length = msg.msg_namelen;
	return ret;
}

void SendDatagram(const int socketFd, const std::span<const std::uint8_t> data, const ngtcp2_addr remote)
{
	iovec iov{
		.iov_base{const_cast<std::uint8_t*>(data.data())},
		.iov_len{data.size()},
	};
	msghdr msg{};
	msg.msg_name = remote.addr;
	msg.msg_namelen = remote.addrlen;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	ssize_t nwrite;
	do
	{
		nwrite = sendmsg(socketFd, &msg, 0);
	} while (nwrite == -1 && errno == EINTR);

	if (nwrite == -1 && errno != EMSGSIZE)
		throw std::system_error{errno, std::generic_category(), "Error sending message"};
}

std::unique_ptr<gnutls_session_int, SessionDeleter> CreateTlsSession(const unsigned int roleFlags,
	const gnutls_certificate_credentials_t credentials, const ConfigureCallback& configureCallback,
	void* connectionReference)
{
	gnutls_session_t tempSession;
	if (const int initRet{gnutls_init(&tempSession, roleFlags | GNUTLS_ENABLE_EARLY_DATA |
		GNUTLS_NO_END_OF_EARLY_DATA)})
	{
		throw std::runtime_error{fmt::format("gnutls_init: {}", gnutls_strerror(initRet))};
	}
	std::unique_ptr<gnutls_session_int, SessionDeleter> session{tempSession};

	if (const int ret{gnutls_priority_set_direct(session.get(), TLS_PRIORITY, nullptr)})
		throw std::runtime_error{fmt::format("gnutls_priority_set_direct: {}", gnutls_strerror(ret))};

	if (const int setRet{gnutls_credentials_set(session.get(), GNUTLS_CRD_CERTIFICATE, credentials)})
		throw std::runtime_error{fmt::format("gnutls_credentials_set: {}", gnutls_strerror(setRet))};

	if (configureCallback(session.get()))
		throw std::runtime_error{"Failed configuring session."};

	gnutls_session_set_ptr(session.get(), connectionReference);

	return session;
}

std::optional<HandshakeError> CheckHandshake(const CSrvHandshakeMessage& serverMessage, const CCliHandshakeMessage& clientMessage)
{
	if (serverMessage.m_EngineVersion != clientMessage.m_EngineVersion)
		return HandshakeError{"engine", clientMessage.m_EngineVersion, serverMessage.m_EngineVersion};

	const auto& serverMods = serverMessage.m_EnabledMods;
	const auto& clientMods = clientMessage.m_EnabledMods;

	for (uint32_t i = 0; i < std::max(clientMods.size(), serverMods.size()); ++i)
	{
		if (i >= serverMods.size())
			return HandshakeError{"mod", GetModName(clientMods[i]), ""};

		if (i >= clientMods.size())
			return HandshakeError{"mod", "", GetModName(serverMods[i])};

		const std::string serverMod = GetModName(serverMods[i]);
		const std::string clientMod = GetModName(clientMods[i]);

		// Client and server have different mods enabled, or mods enabled in a different order
		if (clientMod != serverMod)
			return HandshakeError{"mod", clientMod, serverMod};
	}

	return {};
}

ngtcp2_tstamp Timestamp()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

ngtcp2_callbacks InitCallbacks()
{
	ngtcp2_callbacks callbacks{};
	callbacks.recv_crypto_data = &ngtcp2_crypto_recv_crypto_data_cb;
	callbacks.encrypt = &ngtcp2_crypto_encrypt_cb;
	callbacks.decrypt = &ngtcp2_crypto_decrypt_cb;
	callbacks.hp_mask = &ngtcp2_crypto_hp_mask_cb;
	callbacks.recv_retry = &ngtcp2_crypto_recv_retry_cb;
	callbacks.rand = [](std::uint8_t* destination, const std::size_t destinationLength,
		const ngtcp2_rand_ctx*)
	{
		if (const int ret{gnutls_rnd(GNUTLS_RND_RANDOM, destination, destinationLength)})
			LOGERROR("Failed generating random data for ngtcp2: {}", gnutls_strerror(ret));
	};
	callbacks.get_new_connection_id = [](ngtcp2_conn*, ngtcp2_cid* cid, std::uint8_t* token,
		const std::size_t cidlen, void*)
	{
		if (const int ret{gnutls_rnd(GNUTLS_RND_RANDOM, cid->data, cidlen)})
			LOGERROR("Failed generating random cid: {}", gnutls_strerror(ret));

		cid->datalen = cidlen;

		if (const int ret{gnutls_rnd(GNUTLS_RND_RANDOM, token, NGTCP2_STATELESS_RESET_TOKENLEN)})
			LOGERROR("Failed generating stateles reset token: {}", gnutls_strerror(ret));

		return 0;
	};
	callbacks.update_key = &ngtcp2_crypto_update_key_cb;
	callbacks.delete_crypto_aead_ctx = &ngtcp2_crypto_delete_crypto_aead_ctx_cb;
	callbacks.delete_crypto_cipher_ctx = &ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
	callbacks.get_path_challenge_data = &ngtcp2_crypto_get_path_challenge_data_cb;
	callbacks.version_negotiation = &ngtcp2_crypto_version_negotiation_cb;
	return callbacks;
}

namespace
{
void QuicLog(void*, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}
}

ngtcp2_settings InitSettings()
{
	ngtcp2_settings settings;
	ngtcp2_settings_default(&settings);
	settings.initial_ts = Timestamp();

	if constexpr (ENABLE_NGTCP2_LOGGING)
		settings.log_printf = QuicLog;

	return settings;
}

ngtcp2_transport_params InitTransportParams()
{
	ngtcp2_transport_params params;
	ngtcp2_transport_params_default(&params);
	params.initial_max_data = 1 * MiB;
	params.max_udp_payload_size = MAX_UDP_PAYLOAD_SIZE;
	params.grease_quic_bit = 1;
	return params;
}

ngtcp2_cid GenerateRandomCid()
{
	static_assert(CID_LENGTH >= NGTCP2_MIN_INITIAL_DCIDLEN);
	static_assert(CID_LENGTH <= NGTCP2_MAX_CIDLEN);

	ngtcp2_cid cid{ .datalen{CID_LENGTH} };
	const int ret{gnutls_rnd(GNUTLS_RND_RANDOM, cid.data, CID_LENGTH)};
	if (ret < 0)
		throw std::runtime_error{fmt::format("Failed generating cid: {}", gnutls_strerror(ret))};

	return cid;
}

void WriteStream(const int socketFd, ngtcp2_conn& connection,
	Stream* stream, const ngtcp2_addr remote,
	std::chrono::steady_clock::time_point& lastSendTime)
{
	std::array<std::uint8_t, MAX_UDP_PAYLOAD_SIZE> buffer;
	ngtcp2_pkt_info pi;
	const std::uint64_t ts{Timestamp()};

	while (true)
	{
		ngtcp2_vec datav;
		std::int64_t streamId;
		std::uint32_t flags;

		const auto bytesToSend = stream ? stream->PeekData() : std::nullopt;
		if (bytesToSend.has_value())
		{
			datav.base = const_cast<uint8_t*>(bytesToSend->data());
			datav.len = bytesToSend->size();
			streamId = stream->GetId();
			flags = NGTCP2_WRITE_STREAM_FLAG_MORE;
		}
		else
		{
			/* No stream data to be sent */
			datav.base = nullptr;
			datav.len = 0;
			streamId = -1;
			flags = stream ? 0 : NGTCP2_WRITE_STREAM_FLAG_MORE;
		}

		ngtcp2_ssize streamDataWritten;
		ngtcp2_ssize datagramPayloadSize{ngtcp2_conn_writev_stream(&connection, nullptr, &pi,
			buffer.data(), buffer.size(), &streamDataWritten, flags, streamId, &datav, 1, ts)};

		const auto markStreamdataSent = [&]
		{
			if (stream && streamDataWritten > 0)
				stream->MarkSent(streamDataWritten);
		};

		if (datagramPayloadSize == 0)
			break;
		if (datagramPayloadSize == NGTCP2_ERR_STREAM_DATA_BLOCKED)
			break;
		if (datagramPayloadSize == NGTCP2_ERR_WRITE_MORE)
		{
			markStreamdataSent();
			continue;
		}
		if (datagramPayloadSize < 0)
		{
			throw std::runtime_error{fmt::format("ngtcp2_conn_writev_stream: {}",
				ngtcp2_strerror(static_cast<int>(datagramPayloadSize)))};
		}
		markStreamdataSent();
		try
		{
			SendDatagram(socketFd, {buffer.data(), static_cast<std::size_t>(
				datagramPayloadSize)}, remote);
			lastSendTime = std::chrono::steady_clock::now();
		}
		catch (std::system_error& e)
		{
			if (e.code().value() == EAGAIN || e.code().value() == EWOULDBLOCK)
				break;
			throw;
		}
	}
	ngtcp2_conn_update_pkt_tx_time(&connection, Timestamp());
}

void IncreaseWindow(ngtcp2_conn* conn, const std::uint64_t streamId, const std::uint64_t size)
{
	ngtcp2_conn_extend_max_offset(conn, size);
	ngtcp2_conn_extend_max_stream_offset(conn, streamId, size);
}
