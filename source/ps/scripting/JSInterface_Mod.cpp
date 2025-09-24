/* Copyright (C) 2025 Wildfire Games.
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

#include "JSInterface_Mod.h"

#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Mod.h"
#include "ps/Pyrogenesis.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptRequest.h"

#include <fmt/format.h>
#include <js/Array.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <stdexcept>
#include <vector>

class ScriptInterface;
namespace JS { class CallArgs; }

extern void RestartEngine();

template<>
void Script::ToJSVal(const ScriptRequest& rq, JS::MutableHandleValue ret, const Mod::ModData& data)
{
	ret.set(Script::CreateObject(rq));
	Script::SetProperty(rq, ret, "mod", data.m_Pathname);
	Script::SetProperty(rq, ret, "name", data.m_Name);
	Script::SetProperty(rq, ret, "version", data.m_Version);
	Script::SetProperty(rq, ret, "dependencies", data.m_Dependencies);
	Script::SetProperty(rq, ret, "isCosmetic", data.m_IsCosmetic);
}

template<>
bool Script::FromJSVal(const ScriptRequest& rq, const JS::HandleValue val, Mod::ModData& data)
{
	// This property is not set in mod.json files, so don't fail if it's not there.
	if (Script::HasProperty(rq, val, "mod") && !Script::GetProperty(rq, val, "mod", data.m_Pathname))
		return false;
	if (!Script::GetProperty(rq, val, "name", data.m_Name))
		return false;
	if (!Script::GetProperty(rq, val, "version", data.m_Version))
		return false;
	if (!Script::GetProperty(rq, val, "dependencies", data.m_Dependencies))
		return false;
	// This property is not set in mod.json files, so don't fail if it's not there.
	if (Script::HasProperty(rq, val, "isCosmetic") && !Script::GetProperty(rq, val, "isCosmetic", data.m_IsCosmetic))
		return false;

	return true;
}

JSVAL_VECTOR(Mod::ModData);

// To avoid copying data needlessly in GetEngineInfo, implement To/FromJSVal for pointer types.
using ModDataCPtr = const Mod::ModData*;
template<>
void Script::ToJSVal(const ScriptRequest& rq, JS::MutableHandleValue ret, const ModDataCPtr& data)
{
	Script::ToJSVal(rq, ret, *data);
}
template<>
bool Script::FromJSVal(const ScriptRequest&, const JS::HandleValue, ModDataCPtr&)
{
	// Required by JSVAL_VECTOR, but can't be implemented
	LOGERROR("Not implemented");
	return false;
}
JSVAL_VECTOR(const Mod::ModData*);

namespace JSI_Mod
{
Mod* ModGetter(const ScriptRequest&, JS::CallArgs&)
{
	return &g_Mods;
}

JS::Value GetEngineInfo(const ScriptInterface& scriptInterface)
{
	ScriptRequest rq(scriptInterface);

	JS::RootedValue mods(rq.cx);
	Script::ToJSVal(rq, &mods, g_Mods.GetEnabledModsData());
	JS::RootedValue metainfo(rq.cx);

	Script::CreateObject(
		 rq,
		 &metainfo,
		 "engine_version", engine_version,
		 "mods", mods);

	Script::DeepFreezeObject(rq, metainfo);

	return metainfo;
}

JS::Value GetAvailableModsJSON(const ScriptRequest& rq)
{
	JS::RootedValue ret(rq.cx, Script::CreateObject(rq));
	for (const Mod::ModData& data : g_Mods.GetAvailableMods())
	{
		JS::RootedValue json(rq.cx);
		if (!Script::ParseJSON(rq, data.m_Text, &json))
		{
			throw std::runtime_error{fmt::format("Error parsing mod.json of '{}'",
				data.m_Pathname.c_str())};
		}
		Script::SetProperty(rq, ret, data.m_Pathname.c_str(), json);
	}
	return ret.get();
}

bool AreModsPlayCompatible(const std::vector<Mod::ModData>& a, const std::vector<Mod::ModData>& b)
{
	std::vector<const Mod::ModData*> modsA, modsB;
	modsA.reserve(a.size());
	for (const Mod::ModData& mod : a)
		modsA.push_back(&mod);
	modsB.reserve(b.size());
	for (const Mod::ModData& mod : b)
		modsB.push_back(&mod);
	return Mod::AreModsPlayCompatible(modsA, modsB);
}

bool SetModsAndRestartEngine(const std::vector<CStr>& mods)
{
	if (!g_Mods.EnableMods(mods, false))
		return false;

	RestartEngine();
	return true;
}

bool HasIncompatibleMods()
{
	return g_Mods.GetIncompatibleMods().size() > 0;
}

void RegisterScriptFunctions(const ScriptRequest& rq)
{
	ScriptFunction::Register<GetEngineInfo>(rq, "GetEngineInfo");
	ScriptFunction::Register<GetAvailableModsJSON>(rq, "GetAvailableModsJSON");
	ScriptFunction::Register<&Mod::GetEnabledMods, ModGetter>(rq, "GetEnabledMods");
	ScriptFunction::Register<AreModsPlayCompatible>(rq, "AreModsPlayCompatible");
	ScriptFunction::Register<HasIncompatibleMods> (rq, "HasIncompatibleMods");
	ScriptFunction::Register<&Mod::GetIncompatibleMods, ModGetter>(rq, "GetIncompatibleMods");
	ScriptFunction::Register<&SetModsAndRestartEngine>(rq, "SetModsAndRestartEngine");
}
}
