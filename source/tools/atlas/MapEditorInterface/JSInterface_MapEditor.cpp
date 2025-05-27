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

#include "JSInterface_MapEditor.h"

#include "graphics/GameView.h"
#include "graphics/MapWriter.h"
#include "graphics/MiniMapTexture.h"
#include "graphics/Patch.h"
#include "graphics/RenderableObject.h"
#include "graphics/TerrainTextureManager.h"
#include "HeightmapArray.h"
#include "lib/allocators/shared_ptr.h"
#include "lib/external_libraries/libsdl.h"
#include "lib/file/vfs/vfs.h"
#include "MapEditor.h"
#include "MapEditor_Brushes.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Game.h"
#include "ps/Filesystem.h"
#include "ps/TemplateLoader.h"
#include "ps/World.h"
#include "renderer/Renderer.h"
#include "renderer/SceneRenderer.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "scriptinterface/StructuredClone.h"
#include "simulation2/components/ICmpAIManager.h"
#include "simulation2/components/ICmpMapEditorInterface.h"
#include "simulation2/components/ICmpOwnership.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/components/ICmpTemplateManager.h"
#include "simulation2/helpers/Selection.h"
#include "simulation2/Simulation2.h"
#include "simulation2/system/Entity.h"
#include "TerrainArray.h"

extern void EndGame();
namespace JSI_MapEditor
{
	// Create EditorEngine context
	JSClass class_MapEditor = {
		"MapEditorClass",
		JSCLASS_HAS_RESERVED_SLOTS(1)
	};

	void SetMapSettings(const Script::Interface& scriptInterface, JS::HandleValue data)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		Script::Request rqSim{sim->GetScriptInterface()};
		JS::RootedValue mapSettings{rqSim.cx, Script::CloneValueFromOtherCompartment(sim->GetScriptInterface(), scriptInterface, data)};
		sim->SetMapSettings(mapSettings);
	}

	JS::Value GetMapSettings(const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return JS::UndefinedHandleValue;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		Script::Request rqSim{sim->GetScriptInterface()};
		JS::RootedValue mapSettings{rqSim.cx};
		sim->GetMapSettings(&mapSettings);

		return Script::CloneValueFromOtherCompartment(scriptInterface, sim->GetScriptInterface(), mapSettings);
	}

	bool IsEditorRunning([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		return g_Game && g_Game->IsMapEditor();
	}

	void RevealMap([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		CmpPtr<ICmpRangeManager> cmpRangeManager{*sim, SYSTEM_ENTITY};
		if (cmpRangeManager)
			cmpRangeManager->SetLosRevealWholeMapForAll(true);
	}

	std::vector<std::vector<std::wstring>> GetCivData([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return {};

		CSimulation2* sim = g_Game->GetSimulation2();
		ENSURE(sim);

		CmpPtr<ICmpTemplateManager> cmpTemplateManager{*sim, SYSTEM_ENTITY};
		return cmpTemplateManager->GetCivData();
	}

	JS::Value GetAIData(const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return JS::UndefinedHandleValue;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		Script::Request rqSim{sim->GetScriptInterface()};
		JS::RootedValue aiData{rqSim.cx, ICmpAIManager::GetAIs(sim->GetScriptInterface())};

		return Script::CloneValueFromOtherCompartment(scriptInterface, sim->GetScriptInterface(), aiData);
	}

	void SaveMap([[maybe_unused]] const Script::Interface& scriptInterface, std::wstring filename)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return;

		CMapWriter writer;
		VfsPath pathname = VfsPath(filename).ChangeExtension(L".pmp");
		writer.SaveMap(pathname,
			&g_Game->GetWorld()->GetTerrain(),
			&g_Renderer.GetSceneRenderer().GetWaterManager(), &g_Renderer.GetSceneRenderer().GetSkyManager(),
			&g_LightEnv, g_Game->GetView()->GetCamera(), g_Game->GetView()->GetCinema(),
			&g_Renderer.GetPostprocManager(),
			g_Game->GetSimulation2());
	}

	JS::Value GetInitAttributes(const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return JS::UndefinedHandleValue;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		Script::Request rqSim{sim->GetScriptInterface()};
		JS::RootedValue initSettings{rqSim.cx};
		sim->GetInitAttributes(&initSettings);

		return Script::CloneValueFromOtherCompartment(scriptInterface, sim->GetScriptInterface(), initSettings);
	}

	bool PlaySimulation([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return false;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		std::stringstream simStateStream;
		if (!sim->SerializeState(simStateStream))
		{
			LOGERROR("Can't serialize state");
			return false;
		}

		const VfsPath filename{L"saves/simulation.dat"};
		std::string simStateString{simStateStream.str()};
		std::span<const u8> simStateBuffer{reinterpret_cast<const u8*>(simStateString.data()), simStateString.size()};
		if (g_VFS->CreateFile(filename, simStateBuffer) != INFO::OK)
		{
			LOGERROR("Can't create simulation data: %s", filename.string8().c_str());
			return false;
		}

		g_Game->SetSimRate(1.0);
		return true;
	}

	bool ResumeOrPauseSimulation([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return false;

		float simRate{g_Game->GetSimRate()};
		g_Game->SetSimRate(simRate > 0.0 ? 0.0 : 1.0);
		return true;
	}

	bool StopSimulation([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return false;

		g_Game->SetSimRate(0.0);

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		const VfsPath filename{L"saves/simulation.dat"};
		std::shared_ptr<u8> buffer;
		size_t size;
		if (g_VFS->LoadFile(filename, buffer, size) != INFO::OK)
		{
			LOGERROR("Can't load simulation data: %s", filename.string8().c_str());
			return false;
		}

		std::istringstream s{std::string{reinterpret_cast<const char*>(buffer.get()), size}};
		if (!sim->DeserializeState(s))
		{
			LOGERROR("Can't deserialize state");
			return false;
		}

		return true;
	}

	Brush* CreateBrush(const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return nullptr;

		Brush* brush{new Brush()};

		Script::Request rq{scriptInterface};
		JS::RootedValue obj{rq.cx};

		scriptInterface.GetGlobalProperty(rq, "MapEditor", &obj);
		ENSURE(obj.isObject());

		MapEditor* mapEditor = JS::GetMaybePtrFromReservedSlot<MapEditor>(&obj.toObject(), Script::Interface::JSObjectReservedSlots::PRIVATE);
		ENSURE(mapEditor);

		mapEditor->AddBrush(brush);

		return brush;
	}

	void EndMapEditor(const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return;

		Script::Request rq{scriptInterface};
		JS::RootedValue obj{rq.cx};

		scriptInterface.GetGlobalProperty(rq, "MapEditor", &obj);
		ENSURE(obj.isObject());

		MapEditor* mapEditor = JS::GetMaybePtrFromReservedSlot<MapEditor>(&obj.toObject(), Script::Interface::JSObjectReservedSlots::PRIVATE);
		ENSURE(mapEditor);

		delete mapEditor;

		EndGame();
	}

	void MakeDirtyTiles([[maybe_unused]] const Script::Interface& scriptInterface, int i0, int j0, int i1, int j1)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return;

		g_Game->GetWorld()->GetTerrain().MakeDirty(i0, j0, i1, j1, RENDERDATA_UPDATE_INDICES);
		g_Game->GetView()->GetMiniMapTexture().MakeDirty();
	}

	TerrainTileArray* CreateTerrainTileArray([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return nullptr;

		return new TerrainTileArray();
	}

	HeightmapArray* CreateHeightmapArray([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return nullptr;

		return new HeightmapArray();
	}

	std::vector<std::wstring> GetTerrainGroups([[maybe_unused]] const Script::Interface& scriptInterface)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return {};

		const CTerrainTextureManager::TerrainGroupMap& groups = g_TexMan.GetGroups();
		std::vector<std::wstring> terrainGroups;

		for (CTerrainTextureManager::TerrainGroupMap::const_iterator it = groups.begin(); it != groups.end(); ++it)
			terrainGroups.push_back(it->first.FromUTF8());

		return terrainGroups;
	}

	std::vector<std::wstring> GetTerrainTextures([[maybe_unused]] const Script::Interface& scriptInterface, const std::wstring& groupName)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return {};

		CTerrainGroup* group = g_TexMan.FindGroup(CStrW(groupName).ToUTF8());

		if (!group)
			return {};

		std::vector<std::wstring> terrainTextures;
		for (std::vector<CTerrainTextureEntry*>::const_iterator it = group->GetTerrains().begin(); it != group->GetTerrains().end(); ++it)
			terrainTextures.emplace_back((*it)->GetTag().FromUTF8());

		std::sort(terrainTextures.begin(), terrainTextures.end());

		return terrainTextures;
	}

	std::wstring GetTerrainTexture([[maybe_unused]] const Script::Interface& scriptInterface, const Brush*& brush)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return std::wstring();

		ssize_t x;
		ssize_t y;
		brush->GetCentre(x, y);

		const CTerrain& terrain{g_Game->GetWorld()->GetTerrain()};
		CMiniPatch* const tile{terrain.GetTile(x, y)};

		if (!tile)
			return std::wstring();

		CTerrainTextureEntry* texture = tile->GetTextureEntry();
		if (!texture)
			return std::wstring();

		return texture->GetTag().FromUTF8();
	}

	std::vector<std::wstring> GetTemplates([[maybe_unused]] const Script::Interface& scriptInterface, ETemplatesType templateType)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return {};

		CmpPtr<ICmpTemplateManager> cmpTemplateManager{*g_Game->GetSimulation2(), SYSTEM_ENTITY};
		if (!cmpTemplateManager)
			return {};

		std::vector<std::string> names{cmpTemplateManager->FindAllTemplatesType(templateType)};
		if (names.empty())
			return {};

		std::vector<std::wstring> templates;
		for (std::vector<std::string>::iterator it = names.begin(); it != names.end(); ++it)
			templates.push_back(std::wstring(it->begin(), it->end()));

		std::sort(templates.begin(), templates.end());
		return templates;
	}

	JS::Value MapEditorInterfaceCall(const Script::Interface& scriptInterface, const std::wstring& name, JS::HandleValue data)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return JS::UndefinedValue();

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		CmpPtr<ICmpMapEditorInterface> cmpMapEditorInterface{*sim, SYSTEM_ENTITY};
		if (!cmpMapEditorInterface)
			return JS::UndefinedValue();

		Script::Request rqSim{sim->GetScriptInterface()};
		JS::RootedValue arg{rqSim.cx, Script::CloneValueFromOtherCompartment(sim->GetScriptInterface(), scriptInterface, data)};
		JS::RootedValue ret{rqSim.cx};
		cmpMapEditorInterface->ScriptCall(name, arg, &ret);

		return Script::CloneValueFromOtherCompartment(scriptInterface, sim->GetScriptInterface(), ret);
	}

	entity_id_t PickEntityAtPoint([[maybe_unused]] const Script::Interface& scriptInterface, int x, int y, bool selectActors)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return INVALID_ENTITY;
		return EntitySelection::PickEntityAtPoint(*g_Game->GetSimulation2(), g_Game->GetView()->GetCamera(), x, y, INVALID_PLAYER, selectActors);
	}

	std::vector<entity_id_t> PickSimilarEntities([[maybe_unused]] const Script::Interface& scriptInterface, const entity_id_t& ent, bool selectActors)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return {};

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		CmpPtr<ICmpTemplateManager> cmpTemplateManager{*sim, SYSTEM_ENTITY};
		ENSURE(cmpTemplateManager);

		std::string templateName{cmpTemplateManager->GetCurrentTemplateName(ent)};

		player_id_t owner{INVALID_PLAYER};
		CmpPtr<ICmpOwnership> cmpOwnership{*sim, ent};
		if (cmpOwnership)
			owner = cmpOwnership->GetOwner();

		return EntitySelection::PickSimilarEntities(*sim, g_Game->GetView()->GetCamera(), templateName, owner, false, true, selectActors, false);
	}

	std::vector<entity_id_t> PickEntitiesInRect(int x0, int y0, int x1, int y1, bool selectActors)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return {};

		return EntitySelection::PickEntitiesInRect(*g_Game->GetSimulation2(), g_Game->GetView()->GetCamera(), x0, y0, x1, y1, INVALID_PLAYER, selectActors);
	}

	bool CreateEntityWithId([[maybe_unused]] const Script::Interface& scriptInterface, const entity_id_t& ent, const std::wstring& templateName)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return false;

		CSimulation2* sim{g_Game->GetSimulation2()};
		ENSURE(sim);

		return sim->AddEntity(templateName, ent) != INVALID_ENTITY;
	}

	void SendToClipboard(const std::wstring& text)
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return;

		SDL_SetClipboardText(utf8_from_wstring(text).c_str());
	}

	std::wstring GetFromClipboard()
	{
		if (!g_Game || !g_Game->IsMapEditor())
			return std::wstring();

		const char* text{SDL_GetClipboardText()};
		if (!text)
			return std::wstring();

		return wstring_from_utf8(text);
	}

	void RegisterScriptFunctions(const Script::Request& rq, Script::Interface& scriptInterface)
	{
		// Define Brush class.
		scriptInterface.DefineCustomObjectType(&class_MapEditorBrush, nullptr, 0, nullptr, methods_MapEditorBrush, nullptr, nullptr);

		// Define TerrainTileArray class.
		scriptInterface.DefineCustomObjectType(&class_TerrainTileArray, nullptr, 0, nullptr, methods_TerrainTileArray, nullptr, nullptr);

		// Define HeightmapArray class.
		scriptInterface.DefineCustomObjectType(&class_HeightmapArray, nullptr, 0, nullptr, methods_HeightmapArray, nullptr, nullptr);

		JS::PersistentRootedObject scope{rq.cx, JS_DefineObject(rq.cx, rq.glob, "MapEditor", &class_MapEditor, JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)};
		ENSURE(scope);

		JS::SetReservedSlot(scope, Script::Interface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(static_cast<void*>(new MapEditor())));

		Script::Function::Register<&CreateBrush>(rq.cx, scope, "CreateBrush");
		Script::Function::Register<&GetMapSettings>(rq.cx, scope, "GetMapSettings");
		Script::Function::Register<&GetInitAttributes>(rq.cx, scope, "GetInitAttributes");
		Script::Function::Register<&GetCivData>(rq.cx, scope, "GetCivData");
		Script::Function::Register<&GetAIData>(rq.cx, scope, "GetAIData");
		Script::Function::Register<&IsEditorRunning>(rq.cx, scope, "IsEditorRunning");
		Script::Function::Register<&PlaySimulation>(rq.cx, scope, "PlaySimulation");
		Script::Function::Register<&ResumeOrPauseSimulation>(rq.cx, scope, "ResumeOrPauseSimulation");
		Script::Function::Register<&RevealMap>(rq.cx, scope, "RevealMap");
		Script::Function::Register<&SaveMap>(rq.cx, scope, "SaveMap");
		Script::Function::Register<&SetMapSettings>(rq.cx, scope, "SetMapSettings");
		Script::Function::Register<&StopSimulation>(rq.cx, scope, "StopSimulation");
		Script::Function::Register<&EndMapEditor>(rq.cx, scope, "EndMapEditor");
		Script::Function::Register<&MakeDirtyTiles>(rq.cx, scope, "MakeDirtyTiles");
		Script::Function::Register<&CreateTerrainTileArray>(rq.cx, scope, "CreateTerrainTileArray");
		Script::Function::Register<&CreateHeightmapArray>(rq.cx, scope, "CreateHeightmapArray");
		Script::Function::Register<&GetTerrainGroups>(rq.cx, scope, "GetTerrainGroups");
		Script::Function::Register<&GetTerrainTextures>(rq.cx, scope, "GetTerrainTextures");
		Script::Function::Register<&GetTerrainTexture>(rq.cx, scope, "GetTerrainTexture");
		Script::Function::Register<&GetTemplates>(rq.cx, scope, "GetTemplates");
		Script::Function::Register<&MapEditorInterfaceCall>(rq.cx, scope, "MapEditorInterfaceCall");
		Script::Function::Register<&PickEntityAtPoint>(rq.cx, scope, "PickEntityAtPoint");
		Script::Function::Register<&PickSimilarEntities>(rq.cx, scope, "PickSimilarEntities");
		Script::Function::Register<&PickEntitiesInRect>(rq.cx, scope, "PickEntitiesInRect");
		Script::Function::Register<&CreateEntityWithId>(rq.cx, scope, "CreateEntityWithId");
		Script::Function::Register<&SendToClipboard>(rq.cx, scope, "SendToClipboard");
		Script::Function::Register<&GetFromClipboard>(rq.cx, scope, "GetFromClipboard");
	}
}
