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

#include "lib/types.h"
#include "network/NetMessage.h"
#include "ps/CStr.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"

#include <cstddef>
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
		u8* buf = new u8[len+1];
		buf[len] = '!';
		TS_ASSERT_EQUALS(msg.Serialize(buf) - (buf+len), 0);
		TS_ASSERT_EQUALS(buf[len], '!');

		CNetMessage* msg2 = CNetMessageFactory::CreateMessage(buf, len, script);
		TS_ASSERT_STR_EQUALS(((CSimulationMessage*)msg2)->ToString(), "CSimulationMessage { m_Client: 1, m_Player: 2, m_Turn: 3, m_Data: [4] }");

		delete msg2;
		delete[] buf;
	}
};
