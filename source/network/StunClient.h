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

#ifndef STUNCLIENT_H
#define STUNCLIENT_H

#include <string>

class CStr8;

namespace StunClient
{
/**
 * Return the publicly accessible IP of the given socket.
 * This is done by contacting STUN server.
 * The return IP & port should only be considered valid for the give host/socket.
 */
bool FindPublicIP(int socketDescriptor, CStr8& ip, std::uint16_t& port);

/**
 * Send a message to the target server with the given socket.
 * This will open a port on the local gateway (if any) to receive traffic,
 * allowing the recipient to answer (thus 'punching a hole' in the NAT).
 * NB: this assumes consistent NAT, i.e. the outgoing port is always the same for the given client,
 * thus allowing the IP discovered via STUN to be sent to the target server.
 */
void SendHolePunchingMessages(const int socketDescriptor, const std::string& serverAddress,
	std::uint16_t serverPort);

/**
 * Return the local IP.
 * Technically not a STUN method, but convenient to define here.
 */
bool FindLocalIP(CStr8& ip);
}

#endif // STUNCLIENT_H
