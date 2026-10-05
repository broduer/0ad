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
#include "network/NetServerTurnManager.h"
#include "ps/ConfigDB.h"
#include "simulation2/system/TurnManager.h"

#include <vector>

class TestNetServerTurnManager : public CxxTest::TestSuite
{
public:
	void setUp()
	{
		CConfigDB::Initialise();
	}

	void tearDown()
	{
		CConfigDB::Shutdown();
	}

	void test_oos_announced_before_diagnostic_turn()
	{
		RunOosTest(1);
	}

	void test_oos_announced_on_quick_hash_turn()
	{
		RunOosTest(2);
	}

	void RunOosTest(turn_id_t oosTurn)
	{
		struct Message
		{
			NetMessageType type;
			turn_id_t turn;
			turn_id_t dumpTurn;
		};
		std::vector<Message> messages;
		CNetServerTurnManager* activeManager = nullptr;
		CNetServerTurnManager manager{[&](const CNetMessage* message)
		{
			if (message->GetType() == NMT_SYNC_ERROR)
			{
				const auto& sync = static_cast<const CSyncErrorMessage&>(*message);
				TS_ASSERT_EQUALS(sync.m_Turn, oosTurn);
				TS_ASSERT_EQUALS(sync.m_DumpTurn, activeManager->GetReadyTurn() + 1);
				TS_ASSERT_EQUALS(activeManager->GetReadyTurn(), COMMAND_DELAY_MP);
				for (const Message& earlier : messages)
					TS_ASSERT(earlier.type != NMT_END_COMMAND_BATCH || earlier.turn < sync.m_DumpTurn);
				messages.push_back({message->GetType(), sync.m_Turn, sync.m_DumpTurn});
			}
			else
			{
				TS_ASSERT_EQUALS(message->GetType(), NMT_END_COMMAND_BATCH);
				const auto& batch = static_cast<const CEndCommandBatchMessage&>(*message);
				messages.push_back({message->GetType(), batch.m_Turn, 0});
			}
		}};
		activeManager = &manager;
		manager.InitialiseClient(1, 0, false);
		manager.InitialiseClient(2, 0, false);

		// Drive the real release gate to R, without worker threads or sessions.
		manager.m_ClientsData[1].readyTurn = COMMAND_DELAY_MP;
		manager.CheckClientsReady();
		TS_ASSERT(messages.empty());
		manager.m_ClientsData[2].readyTurn = COMMAND_DELAY_MP;
		manager.CheckClientsReady();
		TS_ASSERT_EQUALS(manager.GetReadyTurn(), COMMAND_DELAY_MP);
		TS_ASSERT_EQUALS(messages.size(), 1);
		for (turn_id_t turn = 1; turn < oosTurn; ++turn)
		{
			manager.RecordClientUpdate(1, L"first", turn, "X");
			manager.RecordClientUpdate(2, L"second", turn, "X");
		}
		manager.RecordClientUpdate(1, L"first", oosTurn, "X");
		TS_ASSERT_EQUALS(messages.size(), 1);
		manager.RecordClientUpdate(2, L"second", oosTurn, "Y");
		TS_ASSERT_EQUALS(messages.size(), 2);
		if (messages.size() != 2)
			return;
		TS_ASSERT_EQUALS(messages[1].type, NMT_SYNC_ERROR);
		TS_ASSERT_EQUALS(messages[1].dumpTurn, COMMAND_DELAY_MP + 1);

		manager.m_ClientsData[1].readyTurn = COMMAND_DELAY_MP + 1;
		manager.CheckClientsReady();
		TS_ASSERT_EQUALS(manager.GetReadyTurn(), COMMAND_DELAY_MP);
		TS_ASSERT_EQUALS(messages.size(), 2);
		manager.m_ClientsData[2].readyTurn = COMMAND_DELAY_MP + 1;
		manager.CheckClientsReady();
		TS_ASSERT_EQUALS(manager.GetReadyTurn(), COMMAND_DELAY_MP + 1);
		TS_ASSERT_EQUALS(messages.size(), 3);
		if (messages.size() == 3)
		{
			TS_ASSERT_EQUALS(messages[2].type, NMT_END_COMMAND_BATCH);
			TS_ASSERT_EQUALS(messages[2].turn, messages[1].dumpTurn);
		}
	}
};
