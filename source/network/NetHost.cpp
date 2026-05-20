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

#include "NetHost.h"

#include "lib/debug.h"
#include "lib/external_libraries/enet.h"
#include "network/NetMessage.h"
#include "ps/CLogger.h"

#include <cstddef>
#include <numeric>
#include <vector>

std::vector<std::uint8_t> CNetHost::CreatePacket(const CNetMessage* message)
{
	size_t size = message->GetSerializedLength();

	ENSURE(size); // else we'll fail when accessing the 0th element

	// Adjust buffer for message
	std::vector<std::uint8_t> buffer;
	buffer.resize(size);

	// Save message to internal buffer
	message->Serialize(&buffer[0]);

	return buffer;
}

void CNetHost::Initialize()
{
	int ret = enet_initialize();
	ENSURE(ret == 0);
}

void CNetHost::Deinitialize()
{
	enet_deinitialize();
}
