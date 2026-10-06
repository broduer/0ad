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

// File purpose: Verify that local simulation speed does not affect serialized game state.

#include "lib/self_test.h"

#include "gui/GUIManager.h"
#include "graphics/MapReader.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "maths/Fixed.h"
#include "maths/FixedVector3D.h"
#include "maths/NUSpline.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/Game.h"
#include "ps/Loader.h"
#include "ps/Profile.h"
#include "ps/ProfileViewer.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Request.h"
#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpCinemaManager.h"
#include "simulation2/components/ICmpUnitMotion.h"
#include "simulation2/helpers/CinemaPath.h"
#include "simulation2/system/CmpPtr.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/TurnManager.h"

#include <array>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace
{
template<typename T>
class ScopedGlobal
{
public:
	ScopedGlobal(T& target, T replacement) :
		m_Target(target), m_Previous(std::exchange(target, std::move(replacement)))
	{
	}

	~ScopedGlobal()
	{
		m_Target = std::move(m_Previous);
	}

private:
	T& m_Target;
	T m_Previous;
};

CCinemaPath MakeCinemaPath()
{
	const CFixedVector3D firstNode(fixed::FromInt(1), fixed::FromInt(0), fixed::FromInt(1));
	const CFixedVector3D secondNode(fixed::FromInt(9), fixed::FromInt(0), fixed::FromInt(9));
	const CFixedVector3D targetOffset(fixed::FromInt(3), fixed::FromInt(3), fixed::FromInt(3));

	CCinemaData pathData;
	pathData.m_Name = L"simrate-determinism";
	pathData.m_Timescale = fixed::FromInt(1);
	pathData.m_Orientation = L"target";
	pathData.m_Mode = L"ease_inout";
	pathData.m_Style = L"default";

	TNSpline positionSpline;
	positionSpline.AddNode(firstNode, CFixedVector3D(), fixed::FromInt(0));
	positionSpline.AddNode(secondNode, CFixedVector3D(), fixed::FromInt(10));

	TNSpline targetSpline;
	targetSpline.AddNode(firstNode + targetOffset, CFixedVector3D(), fixed::FromInt(0));
	targetSpline.AddNode(secondNode + targetOffset, CFixedVector3D(), fixed::FromInt(10));

	return CCinemaPath(pathData, positionSpline, targetSpline);
}
}

class TestGame : public CxxTest::TestSuite
{
	struct SimulationState
	{
		turn_id_t turn;
		std::string hash;
		std::string serializedState;
		bool cinemaPlaying;
		fixed cinemaElapsedTime;

		bool operator==(const SimulationState&) const = default;
	};

	std::unique_ptr<CConfigDB> m_ConfigDB;
	std::optional<CProfileViewer> m_ProfileViewer;
	std::optional<CProfileManager> m_ProfileManager;
	std::optional<CXeromycesEngine> m_XeromycesEngine;
	std::optional<Script::Interface> m_GuiScriptInterface;

public:
	void setUp()
	{
		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "mod" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "public" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));

		m_ConfigDB = std::make_unique<CConfigDB>();
		m_ProfileViewer.emplace();
		m_ProfileManager.emplace();
		m_XeromycesEngine.emplace();
		m_GuiScriptInterface.emplace("Engine", "TestGame", *g_ScriptContext);
		g_GUI = new CGUIManager(*g_ScriptContext, *m_GuiScriptInterface);
	}

	void tearDown()
	{
		delete g_GUI;
		m_GuiScriptInterface.reset();
		m_XeromycesEngine.reset();
		m_ProfileManager.reset();
		m_ProfileViewer.reset();
		m_ConfigDB.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir() / "_testcache");
	}

	void test_simulation_state_is_independent_of_sim_rate()
	{
		const auto stateAtHalfSpeed = RunSimulation(0.5f);
		const auto stateAtNormalSpeed = RunSimulation(1.0f);
		const auto stateAtDoubleSpeed = RunSimulation(2.0f);

		TS_ASSERT_EQUALS(stateAtHalfSpeed, stateAtNormalSpeed);
		TS_ASSERT_EQUALS(stateAtNormalSpeed, stateAtDoubleSpeed);
	}

private:
	std::array<SimulationState, 3> RunSimulation(float simRate)
	{
		CGame game(false);
		ScopedGlobal<CGame*> scopedGame(g_Game, &game);

		{
			Script::Request rq(game.GetSimulation2()->GetScriptInterface());
			CMapSummaryReader mapSummaryReader;
			TS_ASSERT_EQUALS(mapSummaryReader.LoadMap(
				L"maps/scenarios/unit_chasing_test.xml"), PSRETURN_OK);
			JS::RootedValue mapData(rq.cx);
			mapSummaryReader.GetMapSettings(game.GetSimulation2()->GetScriptInterface(), &mapData);
			JS::RootedValue settings(rq.cx);
			TS_ASSERT(Script::GetProperty(rq, mapData, "settings", &settings));
			TS_ASSERT(Script::SetProperty(rq, settings, "AISeed", 2345));
			TS_ASSERT(Script::SetProperty(rq, settings, "CheatsEnabled", false));
			TS_ASSERT(Script::SetProperty(rq, settings, "Seed", 1234));
			JS::RootedValue attributes(rq.cx);
			Script::CreateObject(rq, &attributes,
				"mapType", "scenario",
				"map", "maps/scenarios/unit_chasing_test",
				"mapPath", "maps/scenarios/",
				"settings", settings);

			game.StartGame(&attributes, "");
			TS_ASSERT_OK(PS::Loader::NonprogressiveLoad());
			TS_ASSERT_EQUALS(game.ReallyStartGame(), PSRETURN_OK);
		}

		CmpPtr<ICmpCinemaManager> cinema(*game.GetSimulation2(), SYSTEM_ENTITY);
		TS_ASSERT(cinema);
		cinema->AddPath(MakeCinemaPath());
		cinema->PushPathToQueue(L"simrate-determinism");
		cinema->StartPlayingQueue();

		const size_t initialUnitMotionEntityCount =
			game.GetSimulation2()->GetEntitiesWithInterface(IID_UnitMotion).size();

		game.SetSimRate(simRate);
		constexpr std::array<turn_id_t, 3> checkpoints{20, 40, 60};
		std::array<SimulationState, checkpoints.size()> states{};

		for (size_t i = 0; i < checkpoints.size(); ++i)
		{
			const turn_id_t checkpoint = checkpoints[i];
			while (game.GetTurnManager()->GetCurrentTurn() < checkpoint)
			{
				game.Update(DEFAULT_TURN_LENGTH / 1000.0, false);
				TS_ASSERT_LESS_THAN_EQUALS(game.GetTurnManager()->GetCurrentTurn(), checkpoint);
			}

			TS_ASSERT_EQUALS(game.GetTurnManager()->GetCurrentTurn(), checkpoint);

			if (i == 0)
				TS_ASSERT_LESS_THAN(initialUnitMotionEntityCount,
					game.GetSimulation2()->GetEntitiesWithInterface(IID_UnitMotion).size());

			SimulationState& state = states[i];
			state.turn = game.GetTurnManager()->GetCurrentTurn();
			TS_ASSERT(game.GetSimulation2()->ComputeStateHash(state.hash, false));
			std::stringstream serializedState;
			TS_ASSERT(game.GetSimulation2()->SerializeState(serializedState));
			state.serializedState = serializedState.str();
			state.cinemaPlaying = cinema->IsPlayingQueue();
			state.cinemaElapsedTime = cinema->GetActivePathElapsedTime();
		}

		TS_ASSERT(states[0].cinemaPlaying);
		TS_ASSERT(states[1].cinemaPlaying);
		TS_ASSERT(!states[2].cinemaPlaying);

		return states;
	}
};
