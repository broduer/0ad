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

// Pull in the headers from the default precompiled header,
// even if rlinterface doesn't use precompiled headers.
#include "lib/precompiled.h"

#include "rlinterface/RLInterface.h"

#include "gui/GUIManager.h"
#include "lib/debug.h"
#include "lib/types.h"
#include "network/HttpServer.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Errors.h"
#include "ps/Game.h"
#include "ps/GameSetup/GameSetup.h"
#include "ps/Loader.h"
#include "scriptinterface/JSON.h"
#include "ps/TaskManager.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpAIInterface.h"
#include "simulation2/components/ICmpTemplateManager.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/LocalTurnManager.h"
#include "simulation2/system/TurnManager.h"

#include <cstdlib>
#include <fmt/format.h>
#include <httplib.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace RL
{
Interface::Interface(std::string const serverAddress)
{
	LOGMESSAGERENDER("Starting RL interface HTTP server");
	m_HttpServer = PS::Net::createHttpServer();

	m_HttpServer->Post("/reset", [this](const httplib::Request &req, httplib::Response &res) {
		if(req.body.empty())
		{
			res.set_content("No POST data found.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}

		ScenarioConfig scenario;
		scenario.saveReplay = req.has_param("saveReplay");
		scenario.playerID = 1;
		if (req.has_param("playerID"))
		{
				scenario.playerID = std::stoi(req.get_param_value("playerID"));
		}

		scenario.content = req.body;

		const std::string gameState = Reset(std::move(scenario));

		res.set_content(gameState, "text/plain");
	});

	m_HttpServer->Post("/step", [this](const httplib::Request &req, httplib::Response &res) {
		if (!IsGameRunning())
		{
			res.set_content("Game not running. Please create a scenario first.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		std::stringstream postStream(req.body);
		std::string line;
		std::vector<GameCommand> commands;
		while (std::getline(postStream, line, '\n'))
		{
			const std::size_t splitPos = line.find(";");
			if (splitPos == std::string::npos)
				continue;

			GameCommand cmd;
			cmd.playerID = std::stoi(line.substr(0, splitPos));
			cmd.json_cmd = line.substr(splitPos + 1);
			commands.push_back(std::move(cmd));
		}
		const std::string gameState = Step(std::move(commands));
		if (gameState.empty())
		{
			res.set_content("Game not running. Please create a scenario first.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		else
			res.set_content(gameState, "text/plain");
	});

	m_HttpServer->Post("/evaluate", [this](const httplib::Request &req, httplib::Response &res) {
		if (!IsGameRunning())
		{
			res.set_content("Game not running. Please create a scenario first.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		if (req.body.empty())
		{
			res.set_content("No POST data found.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		std::string code{req.body};
		const std::string codeResult = Evaluate(std::move(code));
		if (codeResult.empty())
		{
			res.set_content("Game not running. Please create a scenario first.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		else
			res.set_content(codeResult, "text/plain");
	});

	m_HttpServer->Get("/templates", [this](const httplib::Request &req, httplib::Response &res) {
		if (!IsGameRunning()) {
			res.set_content("Game not running. Please create a scenario first.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		if (req.body.empty())
		{
			res.set_content("No POST data found.", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}
		std::stringstream postStream(req.body);
		std::string line;
		std::vector<std::string> templateNames;
		while (std::getline(postStream, line, '\n'))
			templateNames.push_back(line);

		std::stringstream stream;
		for (std::string templateStr : GetTemplates(templateNames))
			stream << templateStr.c_str() << "\n";
		res.set_content(stream.str(), "text/plain");
	});

	std::size_t sepIndex = serverAddress.find(":");
	if (sepIndex == std::string::npos)
	{
		throw std::invalid_argument{fmt::format("Invalid server address for RL interface '{}'", serverAddress)};
	}
	std::string address = serverAddress.substr(0, sepIndex);
	int port = std::stoi(serverAddress.substr(sepIndex + 1, serverAddress.length() - sepIndex - 1));

	m_HttpServerThread = std::thread([this, address, port](){
		if (!m_HttpServer->listen(address, port))
		{
			LOGERROR("Failed to start http server");
		}
	});
}

Interface::~Interface() {
	m_HttpServer->stop();
	if(m_HttpServerThread.joinable()) {
		m_HttpServerThread.join();
	}
}

// Interactions with the game engine (g_Game) must be done in the main
// thread as there are specific checks for this. We will pass messages
// to the main thread to be applied (ie, "GameMessage"s).
std::string Interface::SendGameMessage(GameMessage&& msg)
{
	std::unique_lock<std::mutex> msgLock(m_MsgLock);
	ENSURE(m_GameMessage.type == GameMessageType::None);
	m_GameMessage = std::move(msg);
	m_MsgApplied.wait(msgLock, [this]() { return m_GameMessage.type == GameMessageType::None; });
	return m_ReturnValue;
}

std::string Interface::Step(std::vector<GameCommand>&& commands)
{
	std::lock_guard<std::mutex> lock(m_Lock);
	return SendGameMessage({ GameMessageType::Commands, std::move(commands) });
}

std::string Interface::Reset(ScenarioConfig&& scenario)
{
	std::lock_guard<std::mutex> lock(m_Lock);
	m_ScenarioConfig = std::move(scenario);
	return SendGameMessage({ GameMessageType::Reset });
}

std::string Interface::Evaluate(std::string&& code)
{
	std::lock_guard<std::mutex> lock(m_Lock);
	m_Code = std::move(code);
	return SendGameMessage({ GameMessageType::Evaluate });
}

std::vector<std::string> Interface::GetTemplates(const std::vector<std::string>& names) const
{
	std::lock_guard<std::mutex> lock(m_Lock);
	CSimulation2& simulation = *g_Game->GetSimulation2();
	CmpPtr<ICmpTemplateManager> cmpTemplateManager(simulation.GetSimContext().GetSystemEntity());

	std::vector<std::string> templates;
	for (const std::string& templateName : names)
	{
		const CParamNode* node = cmpTemplateManager->GetTemplate(templateName);

		if (node != nullptr)
			templates.push_back(node->ToXMLString());
	}

	return templates;
}

bool Interface::TryGetGameMessage(GameMessage& msg)
{
	if (m_GameMessage.type != GameMessageType::None)
	{
		msg = m_GameMessage;
		m_GameMessage = {GameMessageType::None};
		return true;
	}
	return false;
}

void Interface::TryApplyMessage()
{
	const bool isGameStarted = g_Game && g_Game->IsGameStarted();
	if (m_NeedsGameState && isGameStarted)
	{
		m_ReturnValue = GetGameState();
		m_MsgApplied.notify_one();
		m_MsgLock.unlock();
		m_NeedsGameState = false;
	}

	if (!m_MsgLock.try_lock())
		return;

	GameMessage msg;
	if (!TryGetGameMessage(msg))
	{
		m_MsgLock.unlock();
		return;
	}

	ApplyMessage(msg);
}

void Interface::ApplyMessage(const GameMessage& msg)
{
	const static std::string EMPTY_STATE;
	const bool nonVisual = !g_GUI;
	const bool isGameStarted = g_Game && g_Game->IsGameStarted();
	switch (msg.type)
	{
		case GameMessageType::Reset:
		{
			if (isGameStarted)
				EndGame();

			g_Game = new CGame(m_ScenarioConfig.saveReplay);
			Script::Interface& scriptInterface = g_Game->GetSimulation2()->GetScriptInterface();
			Script::Request rq(scriptInterface);
			JS::RootedValue attrs(rq.cx);
			Script::ParseJSON(rq, m_ScenarioConfig.content, &attrs);

			g_Game->SetPlayerID(m_ScenarioConfig.playerID);
			g_Game->StartGame(&attrs, "");

			if (nonVisual)
			{
				PS::Loader::NonprogressiveLoad();
				ENSURE(g_Game->ReallyStartGame() == PSRETURN_OK);
				m_ReturnValue = GetGameState();
				m_MsgApplied.notify_one();
				m_MsgLock.unlock();
			}
			else
			{
				JS::RootedValue initData(rq.cx);
				Script::CreateObject(rq, &initData);
				Script::SetProperty(rq, initData, "attribs", attrs);

				JS::RootedValue playerAssignments(rq.cx);
				Script::CreateObject(rq, &playerAssignments);
				Script::SetProperty(rq, initData, "playerAssignments", playerAssignments);

				g_GUI->SwitchPage(L"page_loading.xml", &scriptInterface, initData);
				m_NeedsGameState = true;
			}
			break;
		}

		case GameMessageType::Commands:
		{
			if (!g_Game)
			{
				m_ReturnValue = EMPTY_STATE;
				m_MsgApplied.notify_one();
				m_MsgLock.unlock();
				return;
			}
			const Script::Interface& scriptInterface = g_Game->GetSimulation2()->GetScriptInterface();
			CLocalTurnManager* turnMgr = static_cast<CLocalTurnManager*>(g_Game->GetTurnManager());

			for (const GameCommand& command : msg.commands)
			{
				Script::Request rq(scriptInterface);
				JS::RootedValue commandJSON(rq.cx);
				Script::ParseJSON(rq, command.json_cmd, &commandJSON);
				turnMgr->PostCommand(command.playerID, commandJSON);
			}

			const u32 deltaRealTime = DEFAULT_TURN_LENGTH;
			if (nonVisual)
			{
				const double deltaSimTime = deltaRealTime * g_Game->GetSimRate();
				const size_t maxTurns = static_cast<size_t>(g_Game->GetSimRate());
				g_Game->GetTurnManager()->Update(deltaSimTime, maxTurns,
					std::bind_front(&CGUIManager::SendEventToAll, g_GUI));
			}
			else
				g_Game->Update(deltaRealTime);

			m_ReturnValue = GetGameState();
			m_MsgApplied.notify_one();
			m_MsgLock.unlock();
			break;
		}
		case GameMessageType::Evaluate:
		{
			if (!g_Game)
			{
				m_ReturnValue = EMPTY_STATE;
				m_MsgApplied.notify_one();
				m_MsgLock.unlock();
				return;
			}
			const Script::Interface& scriptInterface = g_Game->GetSimulation2()->GetScriptInterface();
			Script::Request rq(scriptInterface);
			JS::RootedValue ret(rq.cx);
			scriptInterface.Eval(m_Code.c_str(), &ret);
			m_ReturnValue = Script::StringifyJSON(rq, &ret, false);
			m_MsgApplied.notify_one();
			m_MsgLock.unlock();
			break;
		}
		default:
		break;
	}
}

std::string Interface::GetGameState() const
{
	const Script::Interface& scriptInterface = g_Game->GetSimulation2()->GetScriptInterface();
	const CSimContext simContext = g_Game->GetSimulation2()->GetSimContext();
	CmpPtr<ICmpAIInterface> cmpAIInterface(simContext.GetSystemEntity());
	Script::Request rq(scriptInterface);
	JS::RootedValue state(rq.cx);
	cmpAIInterface->GetFullRepresentation(&state, true);
	return Script::StringifyJSON(rq, &state, false);
}

bool Interface::IsGameRunning() const
{
	return g_Game != nullptr;
}
}
