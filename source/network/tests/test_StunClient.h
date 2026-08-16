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

#include "lib/self_test.h"

#include "network/NetHost.h"
#include "network/NetProtocol.h"
#include "network/StunClient.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"

#include <netdb.h>
#include <string>

class TestStunClient : public CxxTest::TestSuite
{
public:
	void test_local_ip()
	{
		CStr ip;
		TS_ASSERT(StunClient::FindLocalIP(ip));
		// Quick validation that this looks like a valid IP address.
		if (ip.size() < 7 || ip.size() > 15)
		{
			TS_FAIL("StunClient::FindLocalIP did not return a valid IPV4 address: wrong size");
			return;
		}
		int dots = 0;
		for (char c : ip)
		{
			if (c == '.')
				++dots;
			else if (c < '0' || c > '9')
			{
				TS_FAIL("StunClient::FindLocalIP did not return a valid IPV4 address: wrong character");
				return;
			}
		}
		if (dots != 3)
			TS_FAIL("StunClient::FindLocalIP did not return a valid IPV4 address: wrong separators");
	}

	void DISABLED_test_stun()
	{
		// Disabled test -> should return your external IP by connecting to our STUN server.
		CConfigDB::Initialise();
		CStr ip;
		std::uint16_t port;
		g_ConfigDB.SetValueString(CFG_COMMAND, "lobby.stun.server", "lobby.wildfiregames.com");
		g_ConfigDB.SetValueString(CFG_COMMAND, "lobby.stun.port", "3478");

		CreateSocketResult socket{CreateSocket("::1", 3478,
			addrinfo{ .ai_flags{AF_UNSPEC} }, *[](int, const addrinfo&){ return true; })};
		StunClient::FindPublicIP(socket.descriptor, ip, port);
		LOGWARNING("{} {}", ip.c_str(), port);
		CConfigDB::Shutdown();
	}
};
