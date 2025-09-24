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

#include "Mod.h"

#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/path.h"
#include "lib/sysdep/os.h"
#include "lib/utf8.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/GameSetup/CmdLineArgs.h"
#include "ps/GameSetup/Paths.h"
#include "ps/Profiler2.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptRequest.h"

#include <algorithm>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/algorithm/string/split.hpp>
#include <cstddef>
#include <fstream>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#if OS_WIN
#include <filesystem>
#else
#include "lib/os_path.h"
#endif

class ScriptInterface;

namespace
{
/**
 * Global instance of Mod, always exists.
 */
Mod g_ModInstance;

/*
 * Blacklist of VFS folders altering replay/multiplayer compatibility.
 */
std::function<bool(const VfsPath&)> const IsInNonCosmeticFolder = [](const VfsPath& path) {
	std::vector<CStrW> blacklist{ L"autostart/", L"gamesettings/", L"globalscripts/", L"simulation/" };
	for (const CStrW& name : blacklist)
		if ((path / "").string().find(name) == 0)
			return true;
	return false;
};

bool LoadModJSON(const PIVFS& vfs, OsPath modsPath, OsPath mod, std::string& text)
{
#if OS_WIN
	const std::filesystem::path modJsonPath = (modsPath / mod / L"mod.json").fileSystemPath();
#else
	const std::string modJsonPath = OsString(modsPath / mod / L"mod.json");
#endif
	// Attempt to open mod.json first.
	std::ifstream modjson(modJsonPath);
	if (!modjson)
	{
		modjson.close();

		// Fallback: open the archive and read mod.json there.
		// This can take in the hundreds of milliseconds with large mods.
		vfs->Clear();
		if (vfs->Mount(L"", modsPath / mod / "", VFS_MOUNT_MUST_EXIST, VFS_MIN_PRIORITY) < 0)
			return false;

		CVFSFile modinfo;
		if (modinfo.Load(vfs, L"mod.json", false) != PSRETURN_OK)
			return false;

		text = modinfo.GetAsString();

		// Attempt to write the mod.json file so we'll take the fast path next time.
		std::ofstream out_mod_json(modJsonPath);
		if (out_mod_json)
		{
			out_mod_json << text;
			out_mod_json.close();
		}
		else
		{
			// Print a warning - we'll keep trying, which could have adverse effects.
			LOGWARNING("Could not write external mod.json for zipped mod '%s'. The mod should be reinstalled.", mod.string8());
		}
		return true;
	}
	else
	{
		std::stringstream buffer;
		buffer << modjson.rdbuf();
		text = buffer.str();
		return true;
	}
}

bool DetectModCosmetic(const PIVFS& vfs, OsPath modsPath, OsPath mod, Mod::ModData& modData)
{
	vfs->Clear();
	if (vfs->Mount(L"", modsPath / mod / "", VFS_MOUNT_MUST_EXIST, VFS_MIN_PRIORITY) < 0)
		return false;

	DirectoryNames subdirectoryNames;
	if (vfs->GetDirectoryEntries(L"", nullptr, &subdirectoryNames) < 0)
		return false;

	modData.m_IsCosmetic = std::none_of(subdirectoryNames.begin(), subdirectoryNames.end(), IsInNonCosmeticFolder);

	return true;
}

// Helper function for populating the fields of Mod::ModData
bool LoadModData(const ScriptRequest& rq, const PIVFS& vfs, OsPath modsPath, OsPath mod, Mod::ModData& data)
{
	// Load contents of the "mod.json" file and parse them
	std::string text;
	if (!LoadModJSON(vfs, modsPath, mod, text))
		return false;
	JS::RootedValue json(rq.cx);
	if (!Script::ParseJSON(rq, text, &json))
		return false;
	Script::FromJSVal(rq, json, data);

	// Cache the contents of the file
	data.m_Text = text;

	// Save the "folder name" of the mod
	data.m_Pathname = mod.string8();

	// Detect if the mod can be enabled without replay/MP compatibility risks
	// No need to waste time mounting the mod and public mods, they are not cosmetic
	if (data.m_Pathname == "public" || data.m_Pathname == "mod")
		return true;

	if (!DetectModCosmetic(vfs, modsPath, mod, data))
		return false;

	return true;
}

Status ReloadChangedFileCB(void* param, const VfsPath& path)
{
	return static_cast<Mod*>(param)->ReloadChangedFile(path);
}

} // anonymous namespace

Mod& Mod::Instance()
{
	return g_ModInstance;
}

Mod::Mod()
{
	RegisterFileReloadFunc(ReloadChangedFileCB, this);
}

Mod::~Mod()
{
	UnregisterFileReloadFunc(ReloadChangedFileCB, this);
}

const std::vector<CStr>& Mod::GetEnabledMods() const
{
	return m_EnabledMods;
}

const std::vector<CStr>& Mod::GetIncompatibleMods() const
{
	return m_IncompatibleMods;
}

const std::vector<Mod::ModData>& Mod::GetAvailableMods() const
{
	return m_AvailableMods;
}

bool Mod::EnableMods(const std::vector<CStr>& mods, const bool addPublic)
{
	m_IncompatibleMods.clear();
	m_EnabledMods.clear();

	std::unordered_map<CStr, int> counts;
	for (const CStr& mod : mods)
	{
		// Ignore duplicates.
		if (counts.try_emplace(mod, 0).first->second++ > 0)
			continue;
		m_EnabledMods.emplace_back(mod);
	}

	if (addPublic && counts["public"] == 0)
		m_EnabledMods.insert(m_EnabledMods.begin(), "public");

	if (counts["mod"] == 0)
		m_EnabledMods.insert(m_EnabledMods.begin(), "mod");

	m_IncompatibleMods = CheckForIncompatibleMods(m_EnabledMods);

	for (const CStr& mod : m_IncompatibleMods)
		m_EnabledMods.erase(std::find(m_EnabledMods.begin(), m_EnabledMods.end(), mod));

	return m_IncompatibleMods.empty();
}

const std::vector<const Mod::ModData*> Mod::GetEnabledModsData() const
{
	std::vector<const ModData*> loadedMods;
	for (const CStr& mod : m_EnabledMods)
	{
		if (mod == "mod")
			continue;

		std::vector<ModData>::const_iterator it = std::find_if(m_AvailableMods.begin(), m_AvailableMods.end(),
			[&mod](const ModData& modData) { return modData.m_Pathname == mod; });
		if (it == m_AvailableMods.end())
		{
			// This ought to be impossible, but let's handle it anyways since it's not a reason to crash.
			LOGERROR("Unavailable mod '%s' was enabled.", mod);
			continue;
		}

		loadedMods.emplace_back(std::addressof(*it));
	}

	loadedMods.emplace_back(std::addressof(m_UserModData));

	return loadedMods;
}

bool Mod::AreModsPlayCompatible(const std::vector<const Mod::ModData*>& modsA, const std::vector<const Mod::ModData*>& modsB)
{
	// Mods must be loaded in the same order.
	std::vector<const Mod::ModData*>::const_iterator a = modsA.begin();
	std::vector<const Mod::ModData*>::const_iterator b = modsB.begin();

	while (a != modsA.end() || b != modsB.end())
	{
		if (a != modsA.end() && (*a)->m_IsCosmetic)
		{
			++a;
			continue;
		}
		if (b != modsB.end() && (*b)->m_IsCosmetic)
		{
			++b;
			continue;
		}
		// If at this point one of the two lists still contains items, the sizes are different -> fail.
		if (a == modsA.end() || b == modsB.end())
			return false;

		if ((*a)->m_Pathname != (*b)->m_Pathname)
			return false;
		if ((*a)->m_Version != (*b)->m_Version)
			return false;
		++a;
		++b;
	}
	return true;
}

void Mod::UpdateAvailableMods(const ScriptInterface& scriptInterface)
{
	PROFILE2("UpdateAvailableMods");

	m_AvailableMods.clear();
	const Paths paths(g_CmdLineArgs);

	// loop over all possible paths
	OsPath modPath = paths.RData()/"mods";
	OsPath modUserPath = paths.UserData()/"mods";

	DirectoryNames modDirs;
	DirectoryNames modDirsUser;

	GetDirectoryEntries(modPath, NULL, &modDirs);
	// Sort modDirs so that we can do a fast lookup below
	std::sort(modDirs.begin(), modDirs.end());

	PIVFS vfs = CreateVfs();

	ScriptRequest rq(scriptInterface);
	for (DirectoryNames::iterator iter = modDirs.begin(); iter != modDirs.end(); ++iter)
	{
		ModData data;
		if (!LoadModData(rq, vfs, modPath, *iter, data))
			continue;
		// Valid mod data, add it to our structure
		m_AvailableMods.emplace_back(std::move(data));
	}

	GetDirectoryEntries(modUserPath, NULL, &modDirsUser);

	for (DirectoryNames::iterator iter = modDirsUser.begin(); iter != modDirsUser.end(); ++iter)
	{
		// Ignore mods in the user folder if we have already found them in modDirs.
		if (std::binary_search(modDirs.begin(), modDirs.end(), *iter))
			continue;

		ModData data;
		if (!LoadModData(rq, vfs, modUserPath, *iter, data))
			continue;
		// Valid mod data, add it to our structure
		m_AvailableMods.emplace_back(std::move(data));
	}

	// Check if the user mod is cosmetic
	m_UserModData = { "user", "user", "9999" };
	DetectModCosmetic(vfs, modUserPath, L"user", m_UserModData);
}

Status Mod::ReloadChangedFile(const VfsPath& path)
{
	// We do not care if a file was hotloaded in a cosmetic path, since
	// changes in cosmetic paths cannot make a cosmetic mod non-cosmetic.
	// On the other hand, if a non-cosmetic path was touched, the
	// responsible mod must be marked as non-cosmetic.
	if (!IsInNonCosmeticFolder(path))
		return INFO::OK;

	// Find out in which mod the hotloaded file is located
	OsPath realPath;
	Status ret = g_VFS->GetOriginalPath(path, realPath);
	if (ret != INFO::OK)
		return ret;

	CStr mod = realPath.BeforeCommon(path).Parent().Filename().string8();

	// "mod" and "public" are not cosmetic anyway.
	if (mod == "mod" || mod == "public")
		return INFO::OK;

	if (mod == "user")
	{
		m_UserModData.m_IsCosmetic = false;
		return INFO::OK;
	}

	std::vector<ModData>::iterator it = std::find_if(m_AvailableMods.begin(), m_AvailableMods.end(),
		[&mod](const ModData& modData) { return modData.m_Pathname == mod; });

	if (it == m_AvailableMods.end())
	{
		LOGERROR("File %s was hotloaded, but mod %s is not available on disk.", path.string8().c_str(), mod.c_str());
		return ERR::LOGIC;
	}

	it->m_IsCosmetic = false;

	return INFO::OK;
}

std::vector<CStr> Mod::CheckForIncompatibleMods(const std::vector<CStr>& mods) const
{
	std::vector<CStr> incompatibleMods;
	std::unordered_map<CStr, std::vector<CStr>> modDependencies;
	std::unordered_map<CStr, CStr> modNameVersions;
	for (const CStr& mod : mods)
	{
		if (mod == "mod")
			continue;

		std::vector<ModData>::const_iterator it = std::find_if(m_AvailableMods.begin(), m_AvailableMods.end(),
			[&mod](const ModData& modData) { return modData.m_Pathname == mod; });

		if (it == m_AvailableMods.end())
		{
			incompatibleMods.push_back(mod);
			continue;
		}

		modNameVersions.emplace(it->m_Name, it->m_Version);
		modDependencies.emplace(it->m_Pathname, it->m_Dependencies);
	}

	static const std::vector<CStr> toCheck = { "<=", ">=", "=", "<", ">" };
	for (const CStr& mod : mods)
	{
		if (mod == "mod")
			continue;

		const std::unordered_map<CStr, std::vector<CStr>>::iterator res = modDependencies.find(mod);
		if (res == modDependencies.end())
			continue;
		const std::vector<CStr> deps = res->second;
		if (deps.empty())
			continue;

		for (const CStr& dep : deps)
		{
			if (dep.empty())
				continue;
			// 0ad<=0.0.24
			for (const CStr& op : toCheck)
			{
				const int pos = dep.Find(op.c_str());
				if (pos == -1)
					continue;
				//0ad
				const CStr modToCheck = dep.substr(0, pos);
				//0.0.24
				const CStr versionToCheck = dep.substr(pos + op.size());
				const std::unordered_map<CStr, CStr>::iterator it = modNameVersions.find(modToCheck);
				// Could not find the mod, or 0.0.25(0ad) , <=, 0.0.24(required version)
				if (it == modNameVersions.end() || !CompareVersionStrings(it->second, op, versionToCheck))
					incompatibleMods.push_back(mod);
				break;
			}
		}

	}

	return incompatibleMods;
}

bool Mod::CompareVersionStrings(const CStr& version, const CStr& op, const CStr& required) const
{
	std::vector<CStr> versionSplit;
	std::vector<CStr> requiredSplit;
	static const std::string toIgnore = "-,_";
	boost::split(versionSplit, version, boost::is_any_of(toIgnore), boost::token_compress_on);
	boost::split(requiredSplit, required, boost::is_any_of(toIgnore), boost::token_compress_on);
	boost::split(versionSplit, versionSplit[0], boost::is_any_of("."), boost::token_compress_on);
	boost::split(requiredSplit, requiredSplit[0], boost::is_any_of("."), boost::token_compress_on);

	const bool eq = op.Find("=") != -1;
	const bool lt = op.Find("<") != -1;
	const bool gt = op.Find(">") != -1;

	const size_t min = std::min(versionSplit.size(), requiredSplit.size());

	for (size_t i = 0; i < min; ++i)
	{
		const int diff = versionSplit[i].ToInt() - requiredSplit[i].ToInt();
		if ((gt && diff > 0) || (lt && diff < 0))
			return true;

		if ((gt && diff < 0) || (lt && diff > 0) || (eq && diff))
			return false;
	}

	const size_t versionSize = versionSplit.size();
	const size_t requiredSize = requiredSplit.size();
	if (versionSize == requiredSize)
		return eq;
	return versionSize < requiredSize ? lt : gt;
}
