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

#include "network/NetMessage.h"
#include "ps/CStr.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"

#include <cstddef>
#include <memory>
#include <vector>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>

class TestNetMessage : public CxxTest::TestSuite
{
public:
	void test_sim()
	{
		Script::Interface script("Test", "Test", g_ScriptContext);
		Script::Request rq(script);

		JS::RootedValueArray<1> val{rq.cx, JS::ValueArray<1>{JS::NumberValue(4)}};
		CSimulationMessage msg(script, 1, 2, 3,
			JS::RootedValue{rq.cx, JS::ObjectValue(*JS::NewArrayObject(rq.cx, val))});
		TS_ASSERT_STR_EQUALS(msg.ToString(), "CSimulationMessage { m_Client: 1, m_Player: 2, m_Turn: 3, m_Data: [4] }");

		size_t len = msg.GetSerializedLength();
		std::uint8_t* buf = new std::uint8_t[len+1];
		buf[len] = '!';
		TS_ASSERT_EQUALS(msg.Serialize(buf) - (buf+len), 0);
		TS_ASSERT_EQUALS(buf[len], '!');

		CNetMessage* msg2 = CNetMessageFactory::CreateMessage(buf, len, script);
		TS_ASSERT_STR_EQUALS(((CSimulationMessage*)msg2)->ToString(), "CSimulationMessage { m_Client: 1, m_Player: 2, m_Turn: 3, m_Data: [4] }");

		delete msg2;
		delete[] buf;
	}
	void test_sync_error_turns()
	{
		Script::Interface script("Test", "Test", g_ScriptContext);
		CSyncErrorMessage message;
		message.m_Turn = 2;
		message.m_DumpTurn = 6;
		message.m_HashExpected = "expected";
		CSyncErrorMessage::S_m_PlayerNames player;
		player.m_Name = L"player";
		message.m_PlayerNames.push_back(player);
		const size_t length = message.GetSerializedLength();
		std::vector<std::uint8_t> bytes(length + 1, '!');
		TS_ASSERT_EQUALS(message.Serialize(bytes.data()), bytes.data() + length);
		TS_ASSERT_EQUALS(bytes[length], '!');
		std::unique_ptr<CNetMessage> decoded(CNetMessageFactory::CreateMessage(bytes.data(), length, script));
		if (!decoded || decoded->GetType() != NMT_SYNC_ERROR)
		{
			TS_FAIL("sync error message could not be decoded");
			return;
		}
		const auto& result = static_cast<const CSyncErrorMessage&>(*decoded);
		TS_ASSERT_EQUALS(result.m_Turn, 2);
		TS_ASSERT_EQUALS(result.m_DumpTurn, 6);
		TS_ASSERT_EQUALS(result.m_HashExpected, message.m_HashExpected);
		TS_ASSERT_EQUALS(result.m_PlayerNames.size(), 1);
		if (!result.m_PlayerNames.empty())
			TS_ASSERT_EQUALS(result.m_PlayerNames[0].m_Name, L"player");
	}

};
