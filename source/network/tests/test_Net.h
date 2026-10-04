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

#include "gui/GUIManager.h"
#include "lib/debug.h"
#include "lib/external_libraries/enet.h"
#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/os_path.h"
#include "lib/path.h"
#include "lib/posix/posix_types.h"
#include "network/NetClient.h"
#include "network/NetMessage.h"
#include "network/NetServer.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/Game.h"
#include "ps/Loader.h"
#include "ps/Profile.h"
#include "ps/ProfileViewer.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/Context.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "simulation2/Simulation2.h"
#include "simulation2/system/TurnManager.h"

#include <SDL_timer.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <optional>
#include <string>
#include <vector>

namespace
{
void UpdateGame(CGame& game)
{
	game.GetTurnManager()->Update(1.0f, 1, std::bind_front(&CGUIManager::SendEventToAll, g_GUI));
}
}

class TestNetComms : public CxxTest::TestSuite
{
	std::optional<CXeromycesEngine> xeromycesEngine;

public:
	void setUp()
	{
		CConfigDB::Initialise();

		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "mod" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "public" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));
		xeromycesEngine.emplace();

		enet_initialize();
	}

	void tearDown()
	{
		enet_deinitialize();

		xeromycesEngine.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir()/"_testcache");

		CConfigDB::Shutdown();
	}

	bool clients_are_all(const std::vector<CNetClient*>& clients, unsigned int state)
	{
		for (size_t j = 0; j < clients.size(); ++j)
			if (clients[j]->GetCurrState() != state)
				return false;
		return true;
	}

	void connect(const std::vector<CNetClient*>& clients)
	{
		for (size_t i = 0; ; ++i)
		{
//			debug_printf(".");
			for (size_t j = 0; j < clients.size(); ++j)
				clients[j]->Poll();

			if (clients_are_all(clients, NCS_PREGAME))
				break;

			if (i > 20)
			{
				TS_FAIL("connection timeout");
				break;
			}

			SDL_Delay(100);
		}
	}

#if 0
	void disconnect(CNetServer& server, const std::vector<CNetClient*>& clients)
	{
		for (size_t i = 0; ; ++i)
		{
//			debug_printf(".");
			server.Poll();
			for (size_t j = 0; j < clients.size(); ++j)
				clients[j]->Poll();

			if (clients_are_all(clients, NCS_UNCONNECTED))
				break;

			if (i > 20)
			{
				TS_FAIL("disconnection timeout");
				break;
			}

			SDL_Delay(100);
		}
	}
#endif

	void wait(const std::vector<CNetClient*>& clients, size_t msecs)
	{
		for (size_t i = 0; i < msecs/10; ++i)
		{
			for (size_t j = 0; j < clients.size(); ++j)
				clients[j]->Poll();

			SDL_Delay(10);
		}
	}

	// just so that cxxtestgen doesn't complain "No tests defined" if all are disabled
	void test_dummy() {}

	void test_oos_dump_uses_diagnostic_turn()
	{
		RunOosDumpTest(1);
	}

	void test_oos_dump_on_quick_hash_turn()
	{
		RunOosDumpTest(2);
	}

	void test_oos_dump_cancelled_on_disconnect()
	{
		RunOosDumpTest(1, true);
	}

	void RunOosDumpTest(int oosTurn, bool disconnect = false)
	{
		TestStdoutLogger logger;
		CProfileViewer profileViewer;
		CProfileManager profiler;

		CGame witnessGame(false);
		CGame targetGame(false);
		const std::string attributes = R"({"mapType":"random","script":"new_rms_test.js","settings":{"Seed":1,"Size":128,"VictoryConditions":[],"PlayerData":[{"Civ":"gaia"},{"Civ":"athen"},{"Civ":"brit"}]}})";
		CNetServer server{false, PS_DEFAULT_PORT, false, {}, {}, attributes};
		CNetClient witness(&witnessGame, "127.0.0.1", PS_DEFAULT_PORT, L"witness");
		CNetClient target(&targetGame, "127.0.0.1", PS_DEFAULT_PORT, L"target");
		connect({&witness, &target});
		if (witness.GetCurrState() != NCS_PREGAME || target.GetCurrState() != NCS_PREGAME)
		{
			TS_FAIL("clients did not connect");
			return;
		}

		auto waitForState = [](CNetClient& client, unsigned int state)
		{
			for (size_t i = 0; i < 500; ++i)
			{
				client.Poll();
				if (client.GetCurrState() == state)
					return true;
				SDL_Delay(10);
			}
			return false;
		};

		CNetClient& controller = witness.IsController() ? witness : target;
		if (!controller.IsController())
		{
			TS_FAIL("no client is the controller");
			return;
		}
		controller.SendStartGameMessage(attributes);
		for (CNetClient* client : {&witness, &target})
		{
			if (!waitForState(*client, NCS_LOADING))
			{
				TS_FAIL("client did not start loading");
				return;
			}
			if (PS::Loader::NonprogressiveLoad() != INFO::OK)
			{
				TS_FAIL("client could not load the map");
				return;
			}
			client->LoadFinished();
		}
		if (!waitForState(witness, NCS_INGAME) || !waitForState(target, NCS_INGAME))
		{
			TS_FAIL("clients did not enter the game");
			return;
		}

		for (int turn = 1; turn < oosTurn; ++turn)
		{
			UpdateGame(witnessGame);
			UpdateGame(targetGame);
		}
		std::string witnessHash, targetHash;
		if (!witnessGame.GetSimulation2()->ComputeStateHash(witnessHash, false) ||
			!targetGame.GetSimulation2()->ComputeStateHash(targetHash, false) || witnessHash != targetHash)
		{
			TS_FAIL("clients did not start with the same full hash");
			return;
		}
		if (targetGame.GetSimulation2()->AddEntity(L"gaia/ore/temperate_small") == INVALID_ENTITY)
		{
			TS_FAIL("could not create the divergent entity");
			return;
		}
		UpdateGame(witnessGame);
		UpdateGame(targetGame);
		const bool quick = oosTurn != 1 && oosTurn % 20 != 0;
		if (witnessGame.GetTurnManager()->GetCurrentTurn() != oosTurn ||
			targetGame.GetTurnManager()->GetCurrentTurn() != oosTurn ||
			!witnessGame.GetSimulation2()->ComputeStateHash(witnessHash, quick) ||
			!targetGame.GetSimulation2()->ComputeStateHash(targetHash, quick) || witnessHash == targetHash)
		{
			TS_FAIL("clients did not send different hashes for the OOS turn");
			return;
		}

		auto waitForOos = [](CNetClient& client, int turn, int& dumpTurn, std::wstring& dumpPath)
		{
			Script::Request rq(client.GetScriptInterface());
			JS::RootedObject promise{rq.cx, client.GetNextGUIMessage(client.GetScriptInterface())};
			for (size_t i = 0; i < 500; ++i)
			{
				client.Poll();
				g_ScriptContext->RunJobs();
				while (JS::GetPromiseState(promise) != JS::PromiseState::Pending)
				{
					JS::RootedValue message{rq.cx, JS::GetPromiseResult(promise)};
					std::string type;
					if (Script::GetProperty(rq, message, "type", type) && type == "out-of-sync")
					{
						int reportedTurn = -1;
						bool pending = false;
						return Script::GetProperty(rq, message, "turn", reportedTurn) && reportedTurn == turn &&
							Script::GetProperty(rq, message, "dumpTurn", dumpTurn) &&
							Script::GetProperty(rq, message, "dumpPending", pending) && pending &&
							Script::GetProperty(rq, message, "path_oos_dump", dumpPath);
					}
					promise = client.GetNextGUIMessage(client.GetScriptInterface());
					g_ScriptContext->RunJobs();
				}
				SDL_Delay(10);
			}
			return false;
		};
		int witnessDumpTurn = -1, targetDumpTurn = -1;
		std::wstring witnessPath, targetPath;
		if (!waitForOos(witness, oosTurn, witnessDumpTurn, witnessPath))
		{
			TS_FAIL("witness did not receive a scheduled OOS diagnosis");
			return;
		}
		// Neither client has reached D. Remove artifacts left by an earlier test run.
		const OsPath dumpPath(witnessPath);
		std::filesystem::remove(dumpPath.string());
		std::filesystem::remove(dumpPath.ChangeExtension(L".dat").string());

		// The target has not polled since sending its OOS-turn hash.
		UpdateGame(targetGame);
		if (targetGame.GetTurnManager()->GetCurrentTurn() != oosTurn + 1 ||
			!waitForOos(target, oosTurn, targetDumpTurn, targetPath))
		{
			TS_FAIL("delayed target did not receive the OOS diagnosis");
			return;
		}
		TS_ASSERT_EQUALS(witnessDumpTurn, targetDumpTurn);
		TS_ASSERT_EQUALS(witnessPath, targetPath);
		if (targetDumpTurn <= targetGame.GetTurnManager()->GetCurrentTurn())
		{
			TS_FAIL("target learned D only after simulating it");
			return;
		}
		const int diagnosticTurn = targetDumpTurn;
		for (size_t i = 0; i < 500; ++i)
		{
			witness.Poll();
			target.Poll();
			for (CGame* game : {&witnessGame, &targetGame})
				if (game->GetTurnManager()->GetCurrentTurn() < diagnosticTurn - 1)
					UpdateGame(*game);
			if (witnessGame.GetTurnManager()->GetCurrentTurn() == diagnosticTurn - 1 &&
				targetGame.GetTurnManager()->GetCurrentTurn() == diagnosticTurn - 1 &&
				targetGame.GetTurnManager()->GetPendingTurns() >= 2)
				break;
			SDL_Delay(10);
		}
		if (witnessGame.GetTurnManager()->GetCurrentTurn() != diagnosticTurn - 1 ||
			targetGame.GetTurnManager()->GetCurrentTurn() != diagnosticTurn - 1 ||
			targetGame.GetTurnManager()->GetPendingTurns() < 2)
		{
			TS_FAIL("clients could not prepare D and D+1");
			return;
		}
		TS_ASSERT(!std::filesystem::exists(dumpPath.string()));
		targetGame.m_Paused = true;
		targetGame.Update(1.0, false);
		TS_ASSERT_EQUALS(targetGame.GetTurnManager()->GetCurrentTurn(), diagnosticTurn - 1);
		TS_ASSERT(!std::filesystem::exists(dumpPath.string()));
		targetGame.m_Paused = false;

		auto readFile = [](const OsPath& path, bool binary = false)
		{
			std::ifstream file{OsString(path), binary ? std::ios::binary : std::ios::in};
			return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		};
		auto headers = [&](int turn)
		{
			return "oos turn: " + std::to_string(oosTurn) + "\ndump turn: " + std::to_string(turn) +
				"\nnet client turn: " + std::to_string(turn) + "\n";
		};
		UpdateGame(witnessGame);
		TS_ASSERT_EQUALS(witnessGame.GetTurnManager()->GetCurrentTurn(), diagnosticTurn);
		// Capture the witness artifacts before the target can overwrite the shared paths.
		const std::string witnessText = readFile(dumpPath);
		const std::string witnessBinary = readFile(dumpPath.ChangeExtension(L".dat"), true);
		std::ostringstream expectedText, expectedBinary;
		TS_ASSERT(witnessGame.GetSimulation2()->DumpDebugState(expectedText));
		TS_ASSERT(witnessGame.GetSimulation2()->SerializeState(expectedBinary));
		TS_ASSERT(witnessText == headers(diagnosticTurn) + expectedText.str());
		TS_ASSERT(witnessBinary == expectedBinary.str());
		std::filesystem::remove(dumpPath.string());
		std::filesystem::remove(dumpPath.ChangeExtension(L".dat").string());

		// Both turns must run in one call, with the dump taken between them.
		if (disconnect)
			target.DestroyConnection();
		targetGame.GetTurnManager()->Update(1.0f, 2, std::bind_front(&CGUIManager::SendEventToAll, g_GUI));
		TS_ASSERT_EQUALS(targetGame.GetTurnManager()->GetCurrentTurn(), diagnosticTurn + 1);
		if (disconnect)
		{
			TS_ASSERT(!std::filesystem::exists(dumpPath.string()));
			TS_ASSERT(!std::filesystem::exists(dumpPath.ChangeExtension(L".dat").string()));
			return;
		}
		const std::string targetText = readFile(dumpPath);
		const std::string targetBinary = readFile(dumpPath.ChangeExtension(L".dat"), true);
		TS_ASSERT_EQUALS(targetText.substr(0, headers(diagnosticTurn).size()), headers(diagnosticTurn));
		TS_ASSERT(targetText.find("sim turn: " + std::to_string(diagnosticTurn) + "\n") != std::string::npos);
		TS_ASSERT(!targetBinary.empty());
		TS_ASSERT(targetBinary != witnessBinary);
		std::ostringstream laterBinary;
		TS_ASSERT(targetGame.GetSimulation2()->SerializeState(laterBinary));
		TS_ASSERT(targetBinary != laterBinary.str());

		// Advancing again must not overwrite the once-only diagnostic artifacts.
		for (size_t i = 0; i < 500 && targetGame.GetTurnManager()->GetPendingTurns() == 0; ++i)
		{
			target.Poll();
			SDL_Delay(10);
		}
		UpdateGame(targetGame);
		TS_ASSERT_EQUALS(targetGame.GetTurnManager()->GetCurrentTurn(), diagnosticTurn + 2);
		TS_ASSERT(readFile(dumpPath) == targetText);
		TS_ASSERT(readFile(dumpPath.ChangeExtension(L".dat"), true) == targetBinary);

		// Inspect the serialized Timer, not just the dump headers, after later turns have run.
		std::istringstream savedState(targetBinary);
		if (!targetGame.GetSimulation2()->DeserializeState(savedState))
		{
			TS_FAIL("target binary dump could not be deserialized");
			return;
		}
		const Script::Interface& script = targetGame.GetSimulation2()->GetScriptInterface();
		int savedTime = -1;
		TS_ASSERT(script.Eval("Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer).GetTime()", savedTime));
		TS_ASSERT_EQUALS(savedTime, diagnosticTurn * DEFAULT_TURN_LENGTH);
		Script::Request rq(script);
		JS::RootedValue timer{rq.cx};
		TS_ASSERT(script.Eval("Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer)", &timer));
		TS_ASSERT(targetText.find("  Timer:\n    comp: " + Script::ToString(rq, &timer, true)) != std::string::npos);
		TS_TRACE(("oos turn: " + std::to_string(oosTurn) + ", dump/net/sim turn: " +
			std::to_string(diagnosticTurn) + ", final target turn: " + std::to_string(targetGame.GetTurnManager()->GetCurrentTurn()) +
			", serialized time: " + std::to_string(savedTime)).c_str());
		std::filesystem::remove(dumpPath.string());
		std::filesystem::remove(dumpPath.ChangeExtension(L".dat").string());
	}

	void DISABLED_test_basic()
	{
		// This doesn't actually test much, it just runs a very quick multiplayer game
		// and prints a load of debug output so you can see if anything funny's going on

		Script::Interface scriptInterface("Engine", "Test", g_ScriptContext);
		Script::Request rq(scriptInterface);

		TestStdoutLogger logger;

		std::vector<CNetClient*> clients;

		CGame client1Game(false);
		CGame client2Game(false);
		CGame client3Game(false);

		JS::RootedValue attrs(rq.cx);
		Script::CreateObject(
			rq,
			&attrs,
			"mapType", "scenario",
			"map", "maps/scenarios/Saharan Oases",
			"mapPath", "maps/scenarios/",
			"thing", "example");

		CNetServer server{false, PS_DEFAULT_PORT, false, {}, {}, Script::StringifyJSON(rq, &attrs, false)};

		CNetClient client1(&client1Game, "127.0.0.1", PS_DEFAULT_PORT);
		CNetClient client2(&client2Game, "127.0.0.1", PS_DEFAULT_PORT);
		CNetClient client3(&client3Game, "127.0.0.1", PS_DEFAULT_PORT);

		clients.push_back(&client1);
		clients.push_back(&client2);
		clients.push_back(&client3);

		connect(clients);
		debug_printf("%s", client1.TestReadGuiMessages().c_str());

		server.StartGame();
		SDL_Delay(100);
		for (size_t j = 0; j < clients.size(); ++j)
		{
			clients[j]->Poll();
			TS_ASSERT_OK(PS::Loader::NonprogressiveLoad());
			clients[j]->LoadFinished();
		}

		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command]\\n");
			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client2 test sim command]\\n");
			client2Game.GetTurnManager()->PostCommand(cmd);
		}

		wait(clients, 100);
		UpdateGame(client1Game);
		UpdateGame(client2Game);
		UpdateGame(client3Game);
		wait(clients, 100);
		UpdateGame(client1Game);
		UpdateGame(client2Game);
		UpdateGame(client3Game);
		wait(clients, 100);
	}

	void DISABLED_test_rejoin()
	{
		Script::Interface scriptInterface("Engine", "Test", g_ScriptContext);
		Script::Request rq(scriptInterface);

		TestStdoutLogger logger;

		std::vector<CNetClient*> clients;

		CGame client1Game(false);
		CGame client2Game(false);
		CGame client3Game(false);

		JS::RootedValue attrs(rq.cx);
		Script::CreateObject(
			rq,
			&attrs,
			"mapType", "scenario",
			"map", "maps/scenarios/Saharan Oases",
			"mapPath", "maps/scenarios/",
			"thing", "example");

		CNetServer server{false, PS_DEFAULT_PORT, false, {}, {}, Script::StringifyJSON(rq, &attrs, false)};

		CNetClient client1(&client1Game, "127.0.0.1", PS_DEFAULT_PORT, L"alice");
		CNetClient client2(&client2Game, "127.0.0.1", PS_DEFAULT_PORT, L"bob");
		CNetClient client3(&client3Game, "127.0.0.1", PS_DEFAULT_PORT, L"charlie");

		clients.push_back(&client1);
		clients.push_back(&client2);
		clients.push_back(&client3);

		connect(clients);
		debug_printf("%s", client1.TestReadGuiMessages().c_str());

		server.StartGame();
		SDL_Delay(100);
		for (size_t j = 0; j < clients.size(); ++j)
		{
			clients[j]->Poll();
			TS_ASSERT_OK(PS::Loader::NonprogressiveLoad());
			clients[j]->LoadFinished();
		}

		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 1]\\n");

			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		wait(clients, 100);
		UpdateGame(client1Game);
		UpdateGame(client2Game);
		UpdateGame(client3Game);
		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 2]\\n");
			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		debug_printf("==== Disconnecting client 2\n");

		client2.DestroyConnection();
		clients.erase(clients.begin()+1);

		debug_printf("==== Connecting client 2B\n");

		CGame client2BGame(false);
		CNetClient client2B(&client2BGame, "127.0.0.1", PS_DEFAULT_PORT, L"bob");
		clients.push_back(&client2B);

		for (size_t i = 0; ; ++i)
		{
			debug_printf("[%u]\n", client2B.GetCurrState());
			client2B.Poll();
			if (client2B.GetCurrState() == NCS_PREGAME)
				break;

			if (client2B.GetCurrState() == NCS_UNCONNECTED)
			{
				TS_FAIL("connection rejected");
				return;
			}

			if (i > 20)
			{
				TS_FAIL("connection timeout");
				return;
			}

			SDL_Delay(100);
		}

		wait(clients, 100);

		UpdateGame(client1Game);
		UpdateGame(client3Game);
		wait(clients, 100);
		server.SetTurnLength(100);
		UpdateGame(client1Game);
		UpdateGame(client3Game);
		wait(clients, 100);

		// (This SetTurnLength thing doesn't actually detect errors unless you change
		// CTurnManager::TurnNeedsFullHash to always return true)

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 3]\\n");
			client1Game.GetTurnManager()->PostCommand(cmd);
		}


		clients[2]->Poll();
		TS_ASSERT_OK(PS::Loader::NonprogressiveLoad());
		clients[2]->LoadFinished();

		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 4]\\n");

			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		for (size_t i = 0; i < 3; ++i)
		{
			UpdateGame(client1Game);
			UpdateGame(client2BGame);
			UpdateGame(client3Game);
			wait(clients, 100);
		}
	}
};
