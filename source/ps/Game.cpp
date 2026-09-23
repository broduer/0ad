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

#include "Game.h"

#include "graphics/Color.h"
#include "graphics/GameView.h"
#include "graphics/LOSTexture.h"
#include "graphics/ObjectManager.h"
#include "graphics/RenderWorld.h"
#include "graphics/UnitManager.h"
#include "gui/CGUI.h"
#include "gui/GUIManager.h"
#include "lib/code_generation.h"
#include "lib/debug.h"
#include "network/NetClient.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/GameSetup/GameSetup.h"
#include "ps/Loader.h"
#include "ps/Profile.h"
#include "ps/RenderThread.h"
#include "ps/Replay.h"
#include "ps/VideoMode.h"
#include "ps/World.h"
#include "renderer/Renderer.h"
#include "renderer/RenderingOptions.h"
#include "renderer/SceneRenderer.h"
#include "renderer/TimeManager.h"
#include "renderer/WaterManager.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Context.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpPlayer.h"
#include "simulation2/components/ICmpPlayerManager.h"
#include "simulation2/components/ICmpSelectable.h"
#include "simulation2/components/ICmpSoundManager.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/LocalTurnManager.h"
#include "simulation2/system/ReplayTurnManager.h"
#include "simulation2/system/TurnManager.h"
#include "tools/atlas/GameInterface/GameLoop.h"

#include <cstddef>
#include <fstream>
#include <js/RootingAPI.h>
#include <js/Value.h>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

extern GameLoopState* g_AtlasGameLoop;

/**
 * Globally accessible pointer to the CGame object.
 **/
CGame *g_Game=NULL;

void CreateGame(const bool replayLog, const SimulationDebugOptions debugOptions)
{
	ENSURE(!g_Game);
	const RenderThread::Pause pause;
	g_Game = new CGame(replayLog, debugOptions);
}

void DeleteGame()
{
	const RenderThread::Pause pause;
	SAFE_DELETE(g_Game);
	if (CRenderer::IsInitialised())
		g_Renderer.GetSceneRenderer().ResetState();
}

const CStr CGame::EventNameSimulationUpdate = "SimulationUpdate";

/**
 * Constructor
 *
 **/
CGame::CGame(bool replayLog, const SimulationDebugOptions debugOptions):
	m_World(new CWorld(*this)),
	m_Simulation2{new CSimulation2{&m_World->GetUnitManager(), *g_ScriptContext, &m_World->GetTerrain(),
		CSimulation2::DEFAULT_SCRIPTS, debugOptions}},
	// TODO: we need to remove that global dependency. Maybe the game view
	// should be created outside only if needed.
	m_GameView(CRenderer::IsInitialised() ? new CGameView(g_VideoMode.GetBackendDevice(), this) : nullptr),
	m_GameStarted(false),
	m_CheatsEnabled(false),
	m_Paused(false),
	m_SimRate(1.0f),
	m_PlayerID(-1),
	m_ViewedPlayerID(-1),
	m_IsVisualReplay(false),
	m_ReplayStream(NULL)
{
	// TODO: should use CDummyReplayLogger unless activated by cmd-line arg, perhaps?
	if (replayLog)
		m_ReplayLogger = new CReplayLogger(m_Simulation2->GetScriptInterface());
	else
		m_ReplayLogger = new CDummyReplayLogger();

	// Need to set the CObjectManager references after various objects have
	// been initialised, so do it here rather than via the initialisers above.
	if (m_GameView)
	{
		m_World->GetUnitManager().SetObjectManager(m_GameView->GetObjectManager());
		m_Simulation2->SetDisplayed(true);
	}

	m_TurnManager = new CLocalTurnManager(*m_Simulation2, GetReplayLogger()); // this will get replaced if we're a net server/client
}

/**
 * Destructor
 *
 **/
CGame::~CGame()
{
	// Again, the in-game call tree is going to be different to the main menu one.
	if (CProfileManager::IsInitialised())
		g_Profiler.StructuralReset();

	if (m_ReplayLogger && m_GameStarted)
		m_ReplayLogger->SaveMetadata(*m_Simulation2);

	delete m_TurnManager;
	delete m_GameView;
	delete m_Simulation2;
	delete m_World;
	delete m_ReplayLogger;
	delete m_ReplayStream;
}

void CGame::SetTurnManager(CTurnManager* turnManager)
{
	if (m_TurnManager)
		delete m_TurnManager;

	m_TurnManager = turnManager;

	if (m_TurnManager)
		m_TurnManager->SetPlayerID(m_PlayerID);
}

int CGame::LoadVisualReplayData()
{
	ENSURE(m_IsVisualReplay);
	ENSURE(!m_ReplayPath.empty());
	ENSURE(m_ReplayStream);

	CReplayTurnManager* replayTurnMgr = static_cast<CReplayTurnManager*>(GetTurnManager());

	std::uint32_t currentTurn = 0;
	std::string type;
	while ((*m_ReplayStream >> type).good())
	{
		if (type == "turn")
		{
			std::uint32_t turn = 0;
			std::uint32_t turnLength = 0;
			*m_ReplayStream >> turn >> turnLength;
			ENSURE(turn == currentTurn && "You tried to replay a commands.txt file of a rejoined client. Please use the host's file.");
			replayTurnMgr->StoreReplayTurnLength(currentTurn, turnLength);
		}
		else if (type == "cmd")
		{
			player_id_t player;
			*m_ReplayStream >> player;

			std::string line;
			std::getline(*m_ReplayStream, line);
			replayTurnMgr->StoreReplayCommand(currentTurn, player, line);
		}
		else if (type == "hash" || type == "hash-quick")
		{
			bool quick = (type == "hash-quick");
			std::string replayHash;
			*m_ReplayStream >> replayHash;
			replayTurnMgr->StoreReplayHash(currentTurn, replayHash, quick);
		}
		else if (type == "end")
			++currentTurn;
		else
			CancelLoad(L"Failed to load replay data (unrecognized content)");
	}
	SAFE_DELETE(m_ReplayStream);
	m_FinalReplayTurn = currentTurn > 0 ? currentTurn - 1 : 0;
	replayTurnMgr->StoreFinalReplayTurn(m_FinalReplayTurn);
	return 0;
}

bool CGame::StartVisualReplay(const OsPath& replayPath)
{
	debug_printf("Starting to replay %s\n", replayPath.string8().c_str());

	m_IsVisualReplay = true;

	SetTurnManager(new CReplayTurnManager(*m_Simulation2, GetReplayLogger()));

	m_ReplayPath = replayPath;
	m_ReplayStream = new std::ifstream(OsString(replayPath));

	std::string type;
	ENSURE((*m_ReplayStream >> type).good() && type == "start");

	std::string line;
	std::getline(*m_ReplayStream, line);

	const Script::Interface& scriptInterface = m_Simulation2->GetScriptInterface();
	Script::Request rq(scriptInterface);

	JS::RootedValue attribs(rq.cx);
	Script::ParseJSON(rq, line, &attribs);
	StartGame(&attribs, "");

	return true;
}

/**
 * Initializes the game with the set of attributes provided.
 * Makes calls to initialize the game view, world, and simulation objects.
 * Calls are made to facilitate progress reporting of the initialization.
 **/
void CGame::RegisterInit(const JS::HandleValue attribs, const std::string& savedState)
{
	const Script::Interface& scriptInterface = m_Simulation2->GetScriptInterface();
	Script::Request rq(scriptInterface);

	const bool isSavedGame{!savedState.empty()};

	m_Simulation2->SetInitAttributes(attribs);

	std::string mapType;
	Script::GetProperty(rq, attribs, "mapType", mapType);

	JS::RootedValue settings(rq.cx);
	Script::GetProperty(rq, attribs, "settings", &settings);

	if (Script::HasProperty(rq, attribs, "settings") &&
	    Script::HasProperty(rq, settings, "CheatsEnabled"))
	{
		Script::GetProperty(rq, settings, "CheatsEnabled", m_CheatsEnabled);
	}

	// The simulation seeds its generator from the same setting.
	std::uint32_t seed;
	if (Script::HasProperty(rq, attribs, "settings") &&
	    Script::HasProperty(rq, settings, "Seed") &&
	    Script::GetProperty(rq, settings, "Seed", seed))
	{
		m_World->GetUnitManager().SetVisualSeed(seed);
	}

	float speed;
	if (Script::HasProperty(rq, attribs, "gameSpeed"))
	{
		if (Script::GetProperty(rq, attribs, "gameSpeed", speed))
			SetSimRate(speed);
		else
			LOGERROR("GameSpeed could not be parsed.");
	}

	PS::Loader::BeginRegistering();

	PS::Loader::Register([this]
	{
		return m_Simulation2->ProgressiveLoad();
	}, L"Simulation init", 1000);

	// RC, 040804 - GameView needs to be initialized before World, otherwise GameView initialization
	// overwrites anything stored in the map file that gets loaded by CWorld::Initialize with default
	// values.  At the minute, it's just lighting settings, but could be extended to store camera position.
	// Storing lighting settings in the game view seems a little odd, but it's no big deal; maybe move it at
	// some point to be stored in the world object?
	if (m_GameView)
		m_GameView->RegisterInit();

	if (mapType == "random")
	{
		// Load random map attributes
		std::wstring scriptFile;
		Script::GetProperty(rq, attribs, "script", scriptFile);

		m_World->RegisterInitRMS(scriptFile, scriptInterface.GetContext(), settings, m_PlayerID);
	}
	else
	{
		std::wstring mapFile;
		Script::GetProperty(rq, attribs, "map", mapFile);

		m_World->RegisterInit(mapFile, scriptInterface.GetContext(), settings, m_PlayerID);
	}
	if (m_GameView)
		PS::Loader::Register([]() -> PS::Loader::Task
		{
			co_return g_Renderer.GetSceneRenderer().GetWaterManager().LoadWaterTextures();
		}, L"LoadWaterTextures", 80);

	if (isSavedGame)
		PS::Loader::Register(std::bind_front(
			[](CGame* game, const std::string& state) -> PS::Loader::Task
		{
			co_return game->LoadInitialState(state);
		}, this, savedState), L"Loading game", 1000);

	if (m_IsVisualReplay)
		PS::Loader::Register(std::bind_front([](CGame* game) -> PS::Loader::Task
		{
			co_return game->LoadVisualReplayData();
		}, this), L"Loading visual replay data", 1000);

	// Call the script function InitGame only for new games, not saved games
	if (!isSavedGame)
	{
		// Perform some simulation initializations (replace skirmish entities, explore territories, etc.)
		// that needs to be done before setting up the AI and shouldn't be done in Atlas
		if (!g_AtlasGameLoop->running)
		{
			PS::Loader::Register(std::bind_front([](CGame* game) -> PS::Loader::Task
			{
				game->m_Simulation2->PreInitGame();
				co_return 0;
			}, this), L"PreInitGame", 5000);
		}

		PS::Loader::Register(std::bind_front([](CGame* game) -> PS::Loader::Task
		{
			game->m_Simulation2->InitGame();
			co_return 0;
		}, this), L"InitGame", 4000);
	}

	PS::Loader::EndRegistering();
}

int CGame::LoadInitialState(const std::string& savedState)
{
	std::stringstream stream(savedState);

	bool ok = m_Simulation2->DeserializeState(stream);
	if (!ok)
	{
		CancelLoad(L"Failed to load saved game state. It might have been\nsaved with an incompatible version of the game.");
		return 0;
	}

	return 0;
}

/**
 * Game initialization has been completed. Set game started flag and start the session.
 *
 * @return PSRETURN 0
 **/
PSRETURN CGame::ReallyStartGame()
{
	// We need to do an initial frame step and Interpolate call to set up all
	// the models etc, because Update might never interpolate (e.g. if the game
	// starts paused) and we could end up rendering before having set up any
	// models (so they'd all be invisible). The frame step also flushes the
	// entities destroyed during initialisation.
	PublishFrame(0.0f, 0.0f, 0.0f);
	ApplyRenderPackets();
	Interpolate(0, 0);

	// Run a shrinking GC to reset the memory before starting the game proper.
	// This also clears JIT code, which seems like a good idea as the game init
	// might have different patterns to the game itself.
	m_Simulation2->GetScriptInterface().GetContext().ShrinkingGC();

	m_GameStarted = true;

	// Preload resources to avoid blinking on a first game frame.
	if (CRenderer::IsInitialised())
		g_Renderer.PreloadResourcesBeforeNextFrame();

	if (g_NetClient)
		g_NetClient->LoadFinished();

	// Call the reallyStartGame GUI function, but only if it exists
	if (g_GUI && g_GUI->GetPageCount())
	{
		std::shared_ptr<Script::Interface> scriptInterface = g_GUI->GetActiveGUI()->GetScriptInterface();
		Script::Request rq(scriptInterface);

		JS::RootedValue global(rq.cx, rq.globalValue());
		if (Script::HasProperty(rq, global, "reallyStartGame"))
			Script::Function::CallVoid(rq, global, "reallyStartGame");
	}

	debug_printf("GAME STARTED, ALL INIT COMPLETE\n");

	// The call tree we've built for pregame probably isn't useful in-game.
	if (CProfileManager::IsInitialised())
		g_Profiler.StructuralReset();

	return 0;
}

int CGame::GetPlayerID()
{
	return m_PlayerID;
}

void CGame::SetPlayerID(player_id_t playerID)
{
	m_PlayerID = playerID;
	m_ViewedPlayerID = playerID;

	if (m_TurnManager)
		m_TurnManager->SetPlayerID(m_PlayerID);
}

int CGame::GetViewedPlayerID()
{
	return m_ViewedPlayerID;
}

void CGame::SetViewedPlayerID(player_id_t playerID)
{
	m_ViewedPlayerID = playerID;
	m_PlayerColorsDirty = true;
}

bool CGame::CheatsEnabled() const
{
	return m_CheatsEnabled;
}

void CGame::StartGame(JS::MutableHandleValue attribs, const std::string& savedState)
{
	if (m_ReplayLogger)
		m_ReplayLogger->StartGame(attribs);

	RegisterInit(attribs, savedState);
}

// TODO: doInterpolate is optional because Atlas interpolates explicitly,
// so that it has more control over the update rate. The game might want to
// do the same, and then doInterpolate should be redundant and removed.

void CGame::Update(const double deltaRealTime, bool doInterpolate)
{
	if (m_Paused || !m_TurnManager)
		return;

	const double deltaSimTime = deltaRealTime * m_SimRate;

	if (deltaSimTime)
	{
		// At the normal sim rate, we currently want to render at least one
		// frame per simulation turn, so let maxTurns be 1. But for fast-forward
		// sim rates we want to allow more, so it's not bounded by framerate,
		// so just use the sim rate itself as the number of turns per frame.
		size_t maxTurns = (size_t)m_SimRate;

		if (m_TurnManager->Update(deltaSimTime, maxTurns,
			std::bind_front(&CGUIManager::SendEventToAll, g_GUI)))
		{
			{
				PROFILE3("gui sim update");
				g_GUI->SendEventToAll(EventNameSimulationUpdate);
			}

			GetView()->GetLOSTexture().MakeDirty();
			m_PlayerColorsDirty = true;
		}

		if (CRenderer::IsInitialised())
			m_TimeManagerDelta += deltaSimTime;
	}

	if (doInterpolate)
	{
		const CTurnManager::InterpolationState state{
			m_TurnManager->GetInterpolationState(deltaSimTime, deltaRealTime)};
		m_InterpolationSimFrameLength = state.simFrameLength;
		m_InterpolationOffset = state.offset;
		m_InterpolationRealFrameLength = state.realFrameLength;
	}
}

void CGame::Interpolate(float simFrameLength, float realFrameLength)
{
	if (!m_TurnManager)
		return;

	const CTurnManager::InterpolationState state{m_TurnManager->GetInterpolationState(simFrameLength, realFrameLength)};
	if (m_GameView)
		m_GameView->GetRenderWorld().Interpolate(state.simFrameLength, state.offset, state.realFrameLength, false);
}

void CGame::PushRenderPacket(const RenderWorld::CFramePacket::STiming* timing)
{
	if (!m_GameView)
		return;
	RenderWorld::CPacketQueue& queue{m_GameView->GetPacketQueue()};
	RenderWorld::CFramePacket packet{queue.Recycle()};
	if (!m_Simulation2->TakeFramePacket(packet))
		return;
	packet.los = m_GameView->TakeLosGrid();
	packet.selectionDebugOverlay = ICmpSelectable::ms_EnableDebugOverlays;
	if (timing)
	{
		packet.timing = *timing;
		packet.view = m_GameView->PublishFrameStep();
		if (std::exchange(m_PlayerColorsDirty, false))
			packet.playerColors = GetPlayerColors();
	}
	queue.Push(std::move(packet));
}

void CGame::ApplyRenderPackets()
{
	if (!m_GameView)
		return;
	RenderWorld::CPacketQueue& queue{m_GameView->GetPacketQueue()};
	queue.TakeAll(m_RenderPackets);
	for (RenderWorld::CFramePacket& packet : m_RenderPackets)
	{
		m_GameView->GetRenderWorld().Apply(packet);
		if (packet.view)
			m_GameView->TakePublished(*packet.view);
		if (packet.playerColors)
			m_PlayerColors = std::move(*packet.playerColors);
		if (packet.timing)
		{
			// The lengths and offset are the latest step's; the clocks
			// advance by every step's time.
			const float timeManagerDelta{m_RenderTiming.timeManagerDelta + packet.timing->timeManagerDelta};
			const float realTimeDelta{m_RenderTiming.realTimeDelta + packet.timing->realTimeDelta};
			m_RenderTiming = *packet.timing;
			m_RenderTiming.timeManagerDelta = timeManagerDelta;
			m_RenderTiming.realTimeDelta = realTimeDelta;
		}
	}
	queue.GiveBack(m_RenderPackets);
}

void CGame::FlushRenderPackets()
{
	PushRenderPacket();
	ApplyRenderPackets();
}

void CGame::RefreshRenderFeedback()
{
	if (!m_GameView)
		return;
	RenderWorld::CRenderWorld& renderWorld{m_GameView->GetRenderWorld()};
	m_Simulation2->SetRenderFeedback(renderWorld.GetFeedback());

	std::vector<RenderWorld::SSoundEvent> sounds;
	renderWorld.TakeSoundEvents(sounds);
	if (sounds.empty())
		return;
	CmpPtr<ICmpSoundManager> cmpSoundManager(*m_Simulation2, SYSTEM_ENTITY);
	if (!cmpSoundManager)
		return;
	for (const RenderWorld::SSoundEvent& sound : sounds)
		cmpSoundManager->PlaySoundGroup(sound.soundGroup, sound.entity);
}

void CGame::FrameStep(const float deltaRealTime)
{
	if (!m_TurnManager)
		return;
	const float deltaSimTime = m_Paused ? 0.0f : deltaRealTime * m_SimRate;
	PublishFrame(deltaSimTime, m_InterpolationOffset, deltaRealTime);
}

void CGame::PublishFrame(const float deltaSimTime, const float offset, const float deltaRealTime)
{
	m_Simulation2->PrepareFrameStep();
	if (m_GameView)
		m_GameView->PrepareFrameStep(deltaRealTime);

	RenderThread::WaitForFrameTaken();
	m_Simulation2->FrameStep(deltaSimTime, offset, deltaRealTime);
	const RenderWorld::CFramePacket::STiming timing{m_InterpolationSimFrameLength,
		m_InterpolationOffset, m_InterpolationRealFrameLength,
		std::exchange(m_TimeManagerDelta, 0.0f), deltaRealTime, m_Paused};
	PushRenderPacket(&timing);
}

void CGame::RenderInterpolate()
{
	if (!m_TurnManager || !m_GameView)
		return;
	RenderWorld::CRenderWorld& renderWorld{m_GameView->GetRenderWorld()};
	renderWorld.PublishFeedback();
	m_GameView->GetObjectManager().UpdateOptions(g_RenderingOptions);
	ApplyRenderPackets();
	const RenderWorld::CFramePacket::STiming timing{m_RenderTiming};
	// Frames rendered before the next logic step advance nothing.
	m_RenderTiming.simFrameLength = 0.0f;
	m_RenderTiming.realFrameLength = 0.0f;
	m_RenderTiming.timeManagerDelta = 0.0f;
	m_RenderTiming.realTimeDelta = 0.0f;
	if ((timing.timeManagerDelta != 0.0f || timing.realTimeDelta != 0.0f) && CRenderer::IsInitialised())
		g_Renderer.GetTimeManager().Update(timing.timeManagerDelta, timing.realTimeDelta);
	renderWorld.Interpolate(timing.simFrameLength, timing.offset, timing.realFrameLength, timing.paused);
}

void CGame::RenderInterpolate(const float simFrameLength, const float realFrameLength)
{
	if (!m_TurnManager || !m_GameView)
		return;
	m_GameView->GetRenderWorld().PublishFeedback();
	m_GameView->GetObjectManager().UpdateOptions(g_RenderingOptions);
	FlushRenderPackets();
	m_RenderTiming = {};
	Interpolate(simFrameLength, realFrameLength);
}


static CColor BrokenColor(0.3f, 0.3f, 0.3f, 1.0f);

std::vector<CColor> CGame::GetPlayerColors()
{
	std::vector<CColor> colors;

	CmpPtr<ICmpPlayerManager> cmpPlayerManager(*m_Simulation2, SYSTEM_ENTITY);
	if (!cmpPlayerManager)
		return colors;

	int numPlayers = cmpPlayerManager->GetNumPlayers();
	colors.resize(numPlayers);

	for (int i = 0; i < numPlayers; ++i)
	{
		CmpPtr<ICmpPlayer> cmpPlayer(*m_Simulation2, cmpPlayerManager->GetPlayerByID(i));
		if (!cmpPlayer)
			colors[i] = BrokenColor;
		else
			colors[i] = cmpPlayer->GetDisplayedColor();
	}

	return colors;
}

const CColor& CGame::GetPlayerColor(player_id_t player) const
{
	if (player < 0 || player >= (int)m_PlayerColors.size())
		return BrokenColor;

	return m_PlayerColors[player];
}

bool CGame::IsGameFinished() const
{
	for (const std::pair<entity_id_t, IComponent*>& p : m_Simulation2->GetEntitiesWithInterface(IID_Player))
	{
		CmpPtr<ICmpPlayer> cmpPlayer(*m_Simulation2, p.first);
		if (cmpPlayer && cmpPlayer->HasWon())
			return true;
	}

	return false;
}

// This function is implemented so that mods can't change it's result. See ICmpPlayer.cpp
bool CGame::PlayerFinished(player_id_t playerID) const
{
	CmpPtr<ICmpPlayerManager> cmpPlayerManager(*m_Simulation2, SYSTEM_ENTITY);
	if (!cmpPlayerManager)
		return false;

	CmpPtr<ICmpPlayer> cmpPlayer(*m_Simulation2, cmpPlayerManager->GetPlayerByID(playerID));
	return cmpPlayer && !cmpPlayer->IsActive();
}
