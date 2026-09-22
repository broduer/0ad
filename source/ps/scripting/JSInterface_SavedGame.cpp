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

#include "JSInterface_SavedGame.h"

#include "gui/GUIManager.h"
#include "lib/debug.h"
#include "network/NetClient.h"
#include "network/NetServer.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Game.h"
#include "ps/SavedGame.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Request.h"
#include "scriptinterface/StructuredClone.h"
#include "simulation2/Simulation2.h"
#include "simulation2/system/TurnManager.h"

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <optional>
#include <string>

namespace Script { class Interface; }

namespace JSI_SavedGame
{
JS::Value GetSavedGames(const Script::Interface& scriptInterface)
{
	return SavedGames::GetSavedGames(scriptInterface);
}

bool DeleteSavedGame(const std::wstring& name)
{
	return SavedGames::DeleteSavedGame(name);
}

void SaveGame(const Script::Request& rq, const std::wstring& filename, const std::wstring& description, JS::HandleValue GUIMetadata)
{
	Script::StructuredClone GUIMetadataClone = Script::WriteStructuredClone(rq, GUIMetadata);
	if (SavedGames::Save(filename, description, *g_Game->GetSimulation2(), GUIMetadataClone) < 0)
		LOGERROR("Failed to save game");
}

void SaveGamePrefix(const Script::Request& rq, const std::wstring& prefix, const std::wstring& description, JS::HandleValue GUIMetadata)
{
	Script::StructuredClone GUIMetadataClone = Script::WriteStructuredClone(rq, GUIMetadata);
	if (SavedGames::SavePrefix(prefix, description, *g_Game->GetSimulation2(), GUIMetadataClone) < 0)
		LOGERROR("Failed to save game");
}

void QuickSave(JS::HandleValue GUIMetadata)
{
	if (g_NetServer || g_NetClient)
		LOGERROR("Can't store quicksave during multiplayer!");
	else if (g_Game)
		g_Game->GetTurnManager()->QuickSave(GUIMetadata);
	else
		LOGERROR("Can't store quicksave if game is not running!");
}

void QuickLoad()
{
	if (g_NetServer || g_NetClient)
	{
		LOGERROR("Can't load quicksave during multiplayer!");
		return;
	}
	if (!g_Game)
	{
		LOGERROR("Can't load quicksave if game is not running!");
		return;
	}

	const std::optional<JS::Value> maybeMetadata{g_Game->GetTurnManager()->TryQuickLoad()};

	if (!g_GUI || !maybeMetadata.has_value())
		return;

	const Script::Request rq{g_Game->GetSimulation2()->GetScriptInterface()};
	JS::RootedValue metadata{rq.cx, maybeMetadata.value()};

	// Provide a copy, so that GUI components don't have to clone to get mutable objects
	JS::RootedValue quickSaveMetadataClone(rq.cx, Script::DeepCopy(rq, metadata));

	JS::RootedValueArray<1> paramData(rq.cx);
	paramData[0].set(quickSaveMetadataClone);
	g_GUI->SendEventToAll(CTurnManager::EventNameSavegameLoaded, paramData);

	LOGMESSAGERENDER("Quickloaded game");
}

JS::Value LoadSavedGameMetadata(const Script::Interface& scriptInterface, const std::wstring& name)
{
	std::optional<SavedGames::LoadResult> data{SavedGames::Load(scriptInterface, name)};

	return data ? data->metadata : JS::UndefinedValue();
}

JS::Value StartSavedGame(const Script::Interface& scriptInterface, const std::wstring& name)
{
	// We need to be careful with different compartments and contexts.
	// The GUI calls this function from the GUI context and expects the return value in the same context.
	// The game we start from here creates another context and expects data in this context.

	Script::Request rqGui(scriptInterface);

	ENSURE(!g_NetServer);
	ENSURE(!g_NetClient);

	ENSURE(!g_Game);

	std::optional<SavedGames::LoadResult> data{SavedGames::Load(scriptInterface, name)};
	if (!data)
		return JS::UndefinedValue();

	JS::RootedValue guiContextMetadata{rqGui.cx, data->metadata};

	CreateGame(true);

	{
		CSimulation2* sim = g_Game->GetSimulation2();
		Script::Request rqGame(sim->GetScriptInterface());

		JS::RootedValue gameContextMetadata(rqGame.cx, Script::CloneValueFromOtherCompartment(sim->GetScriptInterface(), scriptInterface, guiContextMetadata));
		JS::RootedValue gameInitAttributes(rqGame.cx);
		Script::GetProperty(rqGame, gameContextMetadata, "initAttributes", &gameInitAttributes);

		int playerID;
		Script::GetProperty(rqGame, gameContextMetadata, "playerID", playerID);

		g_Game->SetPlayerID(playerID);
		g_Game->StartGame(&gameInitAttributes, data->savedState);
	}

	return guiContextMetadata;
}

void ActivateRejoinTest()
{
	if (!g_Game || !g_Game->GetSimulation2() || !g_Game->GetTurnManager())
		return;
	g_Game->GetSimulation2()->ActivateRejoinTest(g_Game->GetTurnManager()->GetCurrentTurn() + 1);
}

void RegisterScriptFunctions(const Script::Request& rq)
{
	Script::Function::Register<&GetSavedGames>(rq, "GetSavedGames");
	Script::Function::Register<&DeleteSavedGame>(rq, "DeleteSavedGame");
	Script::Function::Register<&SaveGame>(rq, "SaveGame");
	Script::Function::Register<&SaveGamePrefix>(rq, "SaveGamePrefix");
	Script::Function::Register<&QuickSave>(rq, "QuickSave");
	Script::Function::Register<&QuickLoad>(rq, "QuickLoad");
	Script::Function::Register<&ActivateRejoinTest>(rq, "ActivateRejoinTest");
	Script::Function::Register<&LoadSavedGameMetadata>(rq, "LoadSavedGameMetadata");
	Script::Function::Register<&StartSavedGame>(rq, "StartSavedGame");
}
}
