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

#ifndef INCLUDED_MOD
#define INCLUDED_MOD

#include "lib/file/vfs/vfs_path.h"
#include "lib/status.h"
#include "ps/CStr.h"

#include <vector>

class ScriptInterface;

#define g_Mods (Mod::Instance())

class Mod
{
	friend class TestMod;
public:
	// Singleton-like interface.
	static Mod& Instance();
	// Use Instance() instead of creating your own instances of Mod
	Mod();
	~Mod();

	/**
	 * Data associated with an available mod, extracted from the mod.json
	 * file. Additional data is used for tracking mod compatibility.
	 */
	struct ModData
	{
		// 'Folder name' of the mod, e.g. 'public' for the main 0 A.D. mod.
		CStr m_Pathname;
		// "name" property in the mod.json, e.g. '0ad' for the main 0 A.D. mod.
		CStr m_Name;

		CStr m_Version;
		std::vector<CStr> m_Dependencies;

		// True if the mod does not touch paths that can affect replay/multiplayer
		// compatibility.
		bool m_IsCosmetic = false;

		// For convenience when the GUI asks for it, cache the contents of the mod.json file.
		// This is dropped when converting to/from a JSVal.
		CStr m_Text;
	};

	const std::vector<CStr>& GetEnabledMods() const;
	const std::vector<CStr>& GetIncompatibleMods() const;
	const std::vector<ModData>& GetAvailableMods() const;

	/**
	 * Fetches available mods and stores some metadata about them.
	 * This may open the zipped mod archives, depending on the situation,
	 * and/or try to write files to the user mod folder,
	 * which can be quite slow, so should be run rarely.
	 * TODO: if this did not need the scriptInterface to parse JSON,
	 * we could run it in different contexts and possibly cleaner.
	 */
	void UpdateAvailableMods(const ScriptInterface& scriptInterface);

	/**
	 * Enables specified mods (& mods required by the engine).
	 * @param addPublic - if true, enable the public mod.
	 * @return whether the mods were enabled successfully. This can fail if e.g. mods are incompatible.
	 * If true, GetEnabledMods() should be non-empty, GetIncompatibleMods() empty. Otherwise, GetIncompatibleMods() is non-empty.
	 */
	bool EnableMods(const std::vector<CStr>& mods, const bool addPublic);

	/**
	 * Get a list of the enabled mod's data (intended for compatibility checks).
	 * The "mod" mod is ignored in the listing.
	 * TODO: for checking against cheat-modding, all listed mods should be zipped
	 * and hashed. The "mod" mod should be hashed as well.
	 */
	const std::vector<const Mod::ModData*> GetEnabledModsData() const;

	/**
	 * @return whether the two lists are compatible for replaying / MP play.
	 */
	static bool AreModsPlayCompatible(const std::vector<const Mod::ModData*>& modsA, const std::vector<const Mod::ModData*>& modsB);

	/*
	 * React to file modifications in the mounted mods.
	 */
	Status ReloadChangedFile(const VfsPath& path);

private:

	/**
	 * Checks a list of @a mods and returns the incompatible mods, if any.
	 */
	std::vector<CStr> CheckForIncompatibleMods(const std::vector<CStr>& mods) const;
	bool CompareVersionStrings(const CStr& required, const CStr& op, const CStr& version) const;

	std::vector<CStr> m_EnabledMods;
	// Of the currently loaded mods, these are the incompatible with the engine and cannot be loaded.
	std::vector<CStr> m_IncompatibleMods;

	std::vector<ModData> m_AvailableMods;
	// The user mod does not have a parsable mod.json, but we must still evaluate if it is cosmetic
	ModData m_UserModData;
};

#endif // INCLUDED_MOD
