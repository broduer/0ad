/* Copyright (C) 2026 Wildfire Games.
 * Copyright (C) 2013-2016 SuperTuxKart-Team.
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

#include "StunClient.h"

#include "lib/byte_order.h"
#include "lib/code_annotation.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"

#include <arpa/inet.h>
#include <bit>
#include <cerrno>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <ranges>
#include <thread>
#include <type_traits>
#include <vector>

namespace StunClient
{

/**
 * These constants are defined in Section 6 of RFC 5389.
 */
const std::uint32_t m_MagicCookie = 0x2112A442;
const std::uint16_t m_MethodTypeBinding = 0x01;
const std::uint32_t m_BindingSuccessResponse = 0x0101;

/**
 * Bit determining whether comprehension of an attribute is optional.
 * Described in Section 15 of RFC 5389.
 */
const std::uint16_t m_ComprehensionOptional = 0x1 << 15;

/**
 * Bit determining whether the bit was assigned by IETF Review.
 * Described in section 18.1. of  RFC 5389.
 */
const std::uint16_t m_IETFReview = 0x1 << 14;

/**
 * These constants are defined in Section 15.1 of RFC 5389.
 */
const std::uint8_t m_IPAddressFamilyIPv4 = 0x01;

/**
 * These constants are defined in Section 18.2 of RFC 5389.
 */
const std::uint16_t m_AttrTypeMappedAddress = 0x001;
const std::uint16_t m_AttrTypeXORMappedAddress = 0x0020;

/**
 * Described in section 3 of RFC 5389.
 */
std::uint8_t m_TransactionID[12];

std::uint32_t m_PublicIp;
std::uint16_t m_PublicPort;

/**
 * Push integral type to a network-byte-order buffer.
 */
template<std::integral T, size_t n = sizeof(T)>
void AddToBuffer(std::vector<std::uint8_t>& buffer, const T value)
{
	buffer.reserve(buffer.size() + n);
	// std::byte* can alias anything so this is legal.
	const std::byte* ptr = reinterpret_cast<const std::byte*>(&value);
	for (size_t a = 0; a < n; ++a)
	{
		if constexpr (std::endian::native == std::endian::little)
			buffer.push_back(static_cast<std::uint8_t>(*(ptr + n - 1 - a)));
		else
			buffer.push_back(static_cast<std::uint8_t>(*(ptr + a)));
	}
}

/**
 * Read integral type from a network-byte-order buffer.
 */
template<std::integral T, size_t n = sizeof(T)>
bool GetFromBuffer(const std::vector<std::uint8_t>& buffer, std::uint32_t& offset, T& result)
{
	if (offset + n > buffer.size())
		return false;

	// std::byte* can alias anything so this is legal.
	std::byte* ptr = reinterpret_cast<std::byte*>(&result);
	for (size_t a = 0; a < n; ++a)
	{
		if constexpr (std::endian::native == std::endian::little)
			*ptr++ = static_cast<std::byte>(buffer[offset + n - 1 - a]);
		else
			*ptr++ = static_cast<std::byte>(buffer[offset + a]);
	}

	offset += n;
	return true;
}

void SendStunRequest(const int socketDescriptor, const sockaddr* serverAddress, const socklen_t addrlen)
{
	std::vector<std::uint8_t> buffer;
	AddToBuffer<std::uint16_t>(buffer, m_MethodTypeBinding);
	AddToBuffer<std::uint16_t>(buffer, 0); // length
	AddToBuffer<std::uint32_t>(buffer, m_MagicCookie);

	for (std::size_t i = 0; i < sizeof(m_TransactionID); ++i)
	{
		std::uint8_t random_byte = rand() % 256;
		buffer.push_back(random_byte);
		m_TransactionID[i] = random_byte;
	}

	for (std::size_t i = 0; i < buffer.size();)
	{
		const ssize_t result{sendto(socketDescriptor, buffer.data(), buffer.size(), 0, serverAddress,
			addrlen)};
		if (result == -1)
		{
			throw std::runtime_error{fmt::format("Error sending STUN request: {}",
				std::strerror(errno))};
		}
		i += result;
	}
}

/**
 * Creates a STUN request and sends it to a STUN server.
 * The request is sent through transactionHost, from which the answer
 * will be retrieved by ReceiveStunResponse and interpreted by ParseStunResponse.
 */
bool CreateStunRequest(const int socket)
{
	const std::string server_name{g_ConfigDB.Get("lobby.stun.server", std::string{})};
	const int port{g_ConfigDB.Get("lobby.stun.port", 0)};

	LOGMESSAGE("StunClient: Using STUN server {}:{}\n", server_name, port);

	addrinfo hints{};
	// IPv4 servers can connect to a IPv6 socket.
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	addrinfo* res;
	if (getaddrinfo(server_name.c_str(), std::to_string(port).c_str(), &hints, &res))
		return -1;
	std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> infoList{res, &freeaddrinfo};
	StunClient::SendStunRequest(socket, res->ai_addr, res->ai_addrlen);

	return true;
}

/**
 * Gets the response from the STUN server and checks it for its validity.
 */
bool ReceiveStunResponse(const int socket, std::vector<std::uint8_t>& buffer)
{
	// TransportAddress sender;
	std::array<char, 2048> inputBuffer{};

	ssize_t len{read(socket, inputBuffer.data(), inputBuffer.size())};

	const int delay{g_ConfigDB.Get("lobby.stun.delay", 10)};
	const int maxTries{g_ConfigDB.Get("lobby.stun.max_tries", 100)};

	// Wait to receive the message.
	for (int count = 0; len <= 0 && (count < maxTries || maxTries == -1); ++count)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		len = read(socket, inputBuffer.data(), inputBuffer.size());
	}

	if (len <= 0)
	{
		LOGERROR("ReceiveStunResponse: recvfrom error ({}): {}", errno, strerror(errno));
		return false;
	}

	// Convert to network string.
	buffer.resize(len);
	std::ranges::transform(inputBuffer | std::views::take(len), buffer.begin(),
		[](const char elem){ return static_cast<std::uint8_t>(elem); });

	return true;
}

bool ParseStunResponse(const std::vector<std::uint8_t>& buffer)
{
	std::uint32_t offset = 0;

	std::uint16_t responseType = 0;
	if (!GetFromBuffer(buffer, offset, responseType) || responseType != m_BindingSuccessResponse)
	{
		LOGERROR("STUN response isn't a binding success response");
		return false;
	}

	// Ignore message size
	offset += 2;

	std::uint32_t cookie = 0;
	if (!GetFromBuffer(buffer, offset, cookie) || cookie != m_MagicCookie)
	{
		LOGERROR("STUN response doesn't contain the magic cookie");
		return false;
	}

	for (std::size_t i = 0; i < sizeof(m_TransactionID); ++i)
	{
		std::uint8_t transactionChar = 0;
		if (!GetFromBuffer(buffer, offset, transactionChar) || transactionChar != m_TransactionID[i])
		{
			LOGERROR("STUN response doesn't contain the transaction ID");
			return false;
		}
	}

	while (offset < buffer.size())
	{
		std::uint16_t type = 0;
		std::uint16_t size = 0;
		if (!GetFromBuffer(buffer, offset, type) ||
		    !GetFromBuffer(buffer, offset, size))
		{
			LOGERROR("STUN response contains invalid attribute");
			return false;
		}

		// The first two bits are irrelevant to the type
		type &= ~(m_ComprehensionOptional | m_IETFReview);

		switch (type)
		{
		case m_AttrTypeMappedAddress:
		case m_AttrTypeXORMappedAddress:
		{
			if (size != 8)
			{
				LOGERROR("Invalid STUN Mapped Address length");
				return false;
			}

			// Ignore the first byte as mentioned in Section 15.1 of RFC 5389.
			++offset;

			std::uint8_t ipFamily = 0;
			if (!GetFromBuffer(buffer, offset, ipFamily) || ipFamily != m_IPAddressFamilyIPv4)
			{
				LOGERROR("Unsupported address family, IPv4 is expected");
				return false;
			}

			std::uint16_t port = 0;
			std::uint32_t ip = 0;
			if (!GetFromBuffer(buffer, offset, port) ||
			    !GetFromBuffer(buffer, offset, ip))
			{
				LOGERROR("Mapped address doesn't contain IP and port");
				return false;
			}

			// Obfuscation is described in Section 15.2 of RFC 5389.
			if (type == m_AttrTypeXORMappedAddress)
			{
				port ^= m_MagicCookie >> 16;
				ip ^= m_MagicCookie;
			}

			m_PublicIp = ip;
			m_PublicPort = port;

			break;
		}
		default:
		{
			// We don't care about other attributes at all

			// Skip attribute
			offset += size;

			// Skip padding
			int padding = size % 4;
			if (padding)
				offset += 4 - padding;
			break;
		}
		}
	}

	return true;
}

bool STUNRequestAndResponse(const int socket)
{
	if (!CreateStunRequest(socket))
		return false;

	std::vector<std::uint8_t> buffer;
	return ReceiveStunResponse(socket, buffer) &&
	       ParseStunResponse(buffer);
}

bool FindPublicIP(const int socket, CStr& ip, std::uint16_t& port)
{
	if (!STUNRequestAndResponse(socket))
		return false;

	ip.resize(INET_ADDRSTRLEN);
	const in_addr ipAddress{htonl(m_PublicIp)};
	if (!inet_ntop(AF_INET, &ipAddress, ip.data(), ip.size()))
	{
		LOGERROR("Invalid ip address: {}", std::strerror(errno));
		return false;
	}

	ip = ip.c_str();
	port = m_PublicPort;

	LOGMESSAGE("StunClient: external IP address is {}:{}", ip, port);

	return true;
}

void SendHolePunchingMessages(const int socketDescriptor, const std::string& serverAddress,
	std::uint16_t serverPort)
{
	const int delay{g_ConfigDB.Get("lobby.fw_punch.delay", 200)};
	const int numMsg{g_ConfigDB.Get("lobby.fw_punch.num_msg", 3)};

	addrinfo hints{};
	// IPv4 servers can connect to a IPv6 socket.
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	addrinfo* res;
	if (const int error{getaddrinfo(serverAddress.c_str(), std::to_string(serverPort).c_str(),
		&hints, &res)})
	{
		throw std::runtime_error{fmt::format("Unable to get host address: {}", std::strerror(error))};
	}
	std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> infoList{res, &freeaddrinfo};

	for (int i = 0; i < numMsg || numMsg == -1; ++i)
	{
		SendStunRequest(socketDescriptor, res->ai_addr, res->ai_addrlen);
		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
	}
}

bool FindLocalIP(CStr& ip)
{
	const int tempSocket{socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
	if (tempSocket < 0)
	{
		LOGERROR("Creating socket: {}", std::strerror(errno));
		return false;
	}

	in_addr testIp;
	if (inet_pton(AF_INET, "100.0.100.0", &testIp) != 1)
	{
		close(tempSocket);
		LOGERROR("Converting IP-string to number: {}", std::strerror(errno));
		return false;
	}
	sockaddr_in testAddr{.sin_family{AF_INET}, .sin_port{2000}, .sin_addr{testIp}};
	if (connect(tempSocket, &reinterpret_cast<sockaddr&>(testAddr), sizeof(testAddr)))
	{
		close(tempSocket);
		LOGERROR("Connecting socket: {}", std::strerror(errno));
		return false;
	}

	sockaddr_storage addr;
	socklen_t addrLen{sizeof(addr)};
	if(getsockname(tempSocket, &reinterpret_cast<sockaddr&>(addr), &addrLen))
	{
		close(tempSocket);
		LOGERROR("Getting IP of socket: {}", std::strerror(errno));
		return false;
	}

	ip.resize(INET_ADDRSTRLEN);
	const in_addr inAddress{reinterpret_cast<sockaddr_in&>(addr).sin_addr};
	if (!inet_ntop(AF_INET, &inAddress, ip.data(), ip.size()))
	{
		close(tempSocket);
		LOGERROR("Invalid ip address: {}", std::strerror(errno));
		return false;
	}
	close(tempSocket);
	ip = ip.c_str();
	return true;
}
}
