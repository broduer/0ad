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

#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/os_path.h"
#include "network/NetClient.h"
#include "network/NetClientTurnManager.h"
#include "ps/CLogger.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/Game.h"
#include "ps/Loader.h"
#include "ps/Profile.h"
#include "ps/ProfileViewer.h"
#include "ps/Replay.h"
#include "ps/Pyrogenesis.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Request.h"
#include "simulation2/Simulation2.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <js/RootingAPI.h>
#include <js/Value.h>

extern CStrW g_UniqueLogPostfix;

class TestNetClientTurnManager : public CxxTest::TestSuite
{
	struct ReplayLogger : CDummyReplayLogger
	{
		bool lastHashWasQuick = false;
		void Hash(const std::string&, bool quick) override { lastHashWasQuick = quick; }
	};

	std::optional<CXeromycesEngine> m_XeromycesEngine;
	CStrW m_LogPostfix;

	static std::string ReadFile(const OsPath& path, bool binary = false)
	{
		std::ifstream file{OsString(path), binary ? std::ios::binary : std::ios::in};
		return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	}

	static OsPath DumpPath()
	{
		return psLogDir() / (L"oos_dump" + g_UniqueLogPostfix + L".txt");
	}

	static void RemoveDumps()
	{
		std::filesystem::remove(DumpPath().string());
		std::filesystem::remove(DumpPath().ChangeExtension(L".dat").string());
	}

public:
	void setUp()
	{
		CConfigDB::Initialise();
		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "mod" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "public" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));
		m_XeromycesEngine.emplace();
		m_LogPostfix = g_UniqueLogPostfix;
		g_UniqueLogPostfix = L"_test_netclientturnmanager";
		RemoveDumps();
	}

	void tearDown()
	{
		RemoveDumps();
		g_UniqueLogPostfix = m_LogPostfix;
		m_XeromycesEngine.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir() / "_testcache");
		CConfigDB::Shutdown();
	}

	void test_oos_dump_uses_diagnostic_turn()
	{
		RunOosDumpTest(1);
	}

	void test_oos_dump_on_quick_hash_turn()
	{
		RunOosDumpTest(2);
	}

	void test_oos_dump_cancelled()
	{
		RunOosDumpTest(1, true);
	}

	void RunOosDumpTest(turn_id_t oosTurn, bool cancel = false)
	{
		TestStdoutLogger logger;
		CProfileViewer profileViewer;
		CProfileManager profiler;
		CGame game(false);
		CSimulation2& simulation = *game.GetSimulation2();
		const Script::Interface& script = simulation.GetScriptInterface();
		Script::Request rq(script);
		JS::RootedValue attributes{rq.cx};
		if (!Script::ParseJSON(rq, R"({"mapType":"random","script":"new_rms_test.js","settings":{"Seed":1,"Size":128,"VictoryConditions":[],"PlayerData":[{"Civ":"gaia"},{"Civ":"athen"},{"Civ":"brit"}]}})", &attributes))
		{
			TS_FAIL("game attributes could not be parsed");
			return;
		}
		game.StartGame(&attributes, "");
		if (PS::Loader::NonprogressiveLoad() != INFO::OK)
		{
			TS_FAIL("game could not load the map");
			return;
		}

		// The private constructor leaves the client without a session or network thread.
		CNetClient client{CNetClient::PrivateTag{}, &game, {}, 0};
		ReplayLogger replay;
		CNetClientTurnManager manager{simulation, client, 1, replay};
		auto update = [&](size_t turns)
		{
			TS_ASSERT(manager.Update(1.0f, turns, [](const auto&, const auto&) {}));
		};
		update(oosTurn);
		TS_ASSERT_EQUALS(manager.GetCurrentTurn(), oosTurn);
		TS_ASSERT_EQUALS(replay.lastHashWasQuick, oosTurn != 1);

		constexpr turn_id_t dumpTurn = COMMAND_DELAY_MP + 1;
		std::string hash;
		if (!simulation.ComputeStateHash(hash, oosTurn != 1))
		{
			TS_FAIL("simulation hash could not be computed");
			return;
		}
		// Deliver the OOS after local progress, then release several turns together.
		update(dumpTurn - 2 - oosTurn);
		TS_ASSERT_EQUALS(manager.GetCurrentTurn(), dumpTurn - 2);
		manager.FinishedAllCommands(COMMAND_DELAY_MP, DEFAULT_TURN_LENGTH);
		manager.OnSyncError(oosTurn, dumpTurn, hash, {});
		TS_ASSERT(!std::filesystem::exists(DumpPath().string()));
		TS_ASSERT(!std::filesystem::exists(DumpPath().ChangeExtension(L".dat").string()));
		for (turn_id_t turn = COMMAND_DELAY_MP + 1; turn <= dumpTurn + 2; ++turn)
			manager.FinishedAllCommands(turn, DEFAULT_TURN_LENGTH);
		update(1);
		TS_ASSERT_EQUALS(manager.GetCurrentTurn(), dumpTurn - 1);
		TS_ASSERT(!std::filesystem::exists(DumpPath().string()));
		TS_ASSERT(!std::filesystem::exists(DumpPath().ChangeExtension(L".dat").string()));

		// D and D+1 finish in one update; the artifacts must capture the intermediate state.
		if (cancel)
			manager.CancelOosDump();
		update(2);
		TS_ASSERT_EQUALS(manager.GetCurrentTurn(), dumpTurn + 1);
		if (cancel)
		{
			TS_ASSERT(!std::filesystem::exists(DumpPath().string()));
			TS_ASSERT(!std::filesystem::exists(DumpPath().ChangeExtension(L".dat").string()));
			return;
		}

		const std::string text = ReadFile(DumpPath());
		const std::string binary = ReadFile(DumpPath().ChangeExtension(L".dat"), true);
		const std::string headers = "oos turn: " + std::to_string(oosTurn) + "\ndump turn: " +
			std::to_string(dumpTurn) + "\nnet client turn: " + std::to_string(dumpTurn) + "\n";
		TS_ASSERT_EQUALS(text.substr(0, headers.size()), headers);
		TS_ASSERT(text.find("sim turn: " + std::to_string(dumpTurn) + "\n") != std::string::npos);
		TS_ASSERT(!binary.empty());
		std::ostringstream laterBinary;
		TS_ASSERT(simulation.SerializeState(laterBinary));
		TS_ASSERT(binary != laterBinary.str());
		// A past write time detects even a rewrite of identical artifact bytes.
		for (const OsPath& path : {DumpPath(), DumpPath().ChangeExtension(L".dat")})
			std::filesystem::last_write_time(path.string(),
				std::filesystem::last_write_time(path.string()) - std::chrono::hours(24));
		const auto textWriteTime = std::filesystem::last_write_time(DumpPath().string());
		const auto binaryWriteTime = std::filesystem::last_write_time(DumpPath().ChangeExtension(L".dat").string());
		update(1);
		TS_ASSERT_EQUALS(manager.GetCurrentTurn(), dumpTurn + 2);
		TS_ASSERT(std::filesystem::last_write_time(DumpPath().string()) == textWriteTime);
		TS_ASSERT(std::filesystem::last_write_time(DumpPath().ChangeExtension(L".dat").string()) == binaryWriteTime);
		TS_ASSERT(ReadFile(DumpPath()) == text);
		TS_ASSERT(ReadFile(DumpPath().ChangeExtension(L".dat"), true) == binary);

		std::istringstream savedState(binary);
		if (!simulation.DeserializeState(savedState))
		{
			TS_FAIL("binary dump could not be deserialized");
			return;
		}
		int savedTime = -1;
		TS_ASSERT(script.Eval("Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer).GetTime()", savedTime));
		TS_ASSERT_EQUALS(savedTime, dumpTurn * DEFAULT_TURN_LENGTH);
		// Deserialization restores components, but not the simulation debug turn counter.
		std::ostringstream restoredText;
		TS_ASSERT(simulation.DumpDebugState(restoredText));
		const std::string restored = restoredText.str();
		TS_ASSERT(text == headers + "sim turn: " + std::to_string(dumpTurn) + "\n" +
			restored.substr(restored.find('\n') + 1));
	}
};
