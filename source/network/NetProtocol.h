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

#ifndef NETPROTOCOL_H
#define NETPROTOCOL_H

#include "network/NetMessage.h"
#include "network/Stream.h"
#include "ps/Mod.h"
#include "ps/Pyrogenesis.h"

#include <chrono>
#include <ngtcp2/ngtcp2.h>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <gnutls/gnutls.h>

/**
 * Report the peer if we didn't receive a packet after this time (milliseconds).
 */
inline constexpr std::uint32_t NETWORK_WARNING_TIMEOUT{2000};
inline constexpr std::size_t MAX_UDP_PAYLOAD_SIZE{64 * KiB};
inline constexpr std::size_t CID_LENGTH{8};
// nanoseconds
inline constexpr ngtcp2_duration KEEPALIVE_TIME{500000000};

struct CredentialsDeleter
{
	void operator()(const gnutls_certificate_credentials_t cred) const
	{
		gnutls_certificate_free_credentials(cred);
	}
};

struct SessionDeleter
{
	void operator()(const gnutls_session_t p) const
	{
		gnutls_deinit(p);
	}
};

struct ConnectionDeleter
{
	void operator()(ngtcp2_conn* p) const
	{
		ngtcp2_conn_del(p);
	}
};

template <typename Message>
Message CreateHandshake() {
	Message handshake;

	if constexpr (std::is_same_v<Message, CCliHandshakeMessage>)
		handshake.m_MagicResponse = PS_PROTOCOL_MAGIC_RESPONSE;
	else
		handshake.m_Magic = PS_PROTOCOL_MAGIC;

	handshake.m_ProtocolVersion = PS_PROTOCOL_VERSION;
	handshake.m_EngineVersion = PS_SERIALIZATION_VERSION;

	for (const Mod::ModData* mod : Mod::Instance().GetEnabledModsData())
	{
		if (mod->m_IgnoreInCompatibilityChecks)
			continue;

		typename Message::S_m_EnabledMods enabledMod;
		enabledMod.m_Name = mod->m_Name;
		enabledMod.m_Version = mod->m_Version;
		handshake.m_EnabledMods.push_back(enabledMod);
	}

	return handshake;
}

struct AddressStorage
{
	ngtcp2_sockaddr_union address;
	ngtcp2_socklen length{sizeof(address)};
};
struct CreateSocketResult
{
	int descriptor;
	AddressStorage address;
};
struct addrinfo;
using SocketTest = bool(int, const addrinfo&);
CreateSocketResult CreateSocket(const char* host, const std::uint16_t port, const addrinfo& hints,
	const SocketTest& socketTest);

std::size_t ReceiveDatagram(const int fd, const std::span<std::uint8_t> data,
	AddressStorage& remoteAddress);
void SendDatagram(const int socketFd, const std::span<const std::uint8_t> data, const ngtcp2_addr remote);

using ConfigureCallback = int(gnutls_session_t);
std::unique_ptr<gnutls_session_int, SessionDeleter> CreateTlsSession(const unsigned int roleFlags,
	const gnutls_certificate_credentials_t credentials, const ConfigureCallback& configureCallback,
	void* connectionReference);

struct HandshakeError
{
	std::string componentType;
	std::string clientComponent;
	std::string serverComponent;
};
std::optional<HandshakeError> CheckHandshake(const CSrvHandshakeMessage& serverMessage,
	const CCliHandshakeMessage& clientMessage);

ngtcp2_tstamp Timestamp();
ngtcp2_callbacks InitCallbacks();
ngtcp2_settings InitSettings();
ngtcp2_transport_params InitTransportParams();

ngtcp2_cid GenerateRandomCid();

void WriteStream(const int socketFd, ngtcp2_conn& connection, Stream* stream, const ngtcp2_addr remote,
	std::chrono::steady_clock::time_point& lastSendTime);

void IncreaseWindow(ngtcp2_conn* conn, const std::uint64_t streamId, const std::uint64_t size);

#endif
