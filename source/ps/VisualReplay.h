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

#ifndef INCLUDED_VISUAL_REPLAY
#define INCLUDED_VISUAL_REPLAY

#include "lib/os_path.h"

#include <js/TypeDecls.h>

class CStrW;
namespace JS { class Value; }
namespace Script { class Interface; }

/**
 * Contains functions for visually replaying past games.
 */
namespace VisualReplay
{

/**
 * Returns the absolute path to the sim-log directory (that contains the directories with the replay files.
 */
OsPath GetDirectoryPath();

/**
 * Returns the absolute path to the replay cache file.
 */
OsPath GetCacheFilePath();

/**
 * Returns the absolute path to the temporary replay cache file used to
 * always have a valid cache file in place even if bad things happen.
 */
OsPath GetTempCacheFilePath();

/**
 * Replays the commands.txt file in the given subdirectory visually.
 */
bool StartVisualReplay(const OsPath& directory);

/**
 * Reads the replay Cache file and parses it into a jsObject
 *
 * @param scriptInterface - the Script::Interface in which to create the return data.
 * @param cachedReplaysObject - the cached replays.
 * @return true on succes
 */
bool ReadCacheFile(const Script::Interface& scriptInterface, JS::MutableHandleObject cachedReplaysObject);

/**
 * Stores the replay list in the replay cache file
 *
 * @param scriptInterface - the Script::Interface in which to create the return data.
 * @param replays - the replay list to store.
 */
void StoreCacheFile(const Script::Interface& scriptInterface, JS::HandleObject replays);

/**
 * Load the replay cache and check if there are new/deleted replays. If so, update the cache.
 *
 * @param scriptInterface - the Script::Interface in which to create the return data.
 * @param compareFiles - compare the directory name and the FileSize of the replays and the cache.
 * @return cache entries
 */
JS::HandleObject ReloadReplayCache(const Script::Interface& scriptInterface, bool compareFiles);

/**
 * Get a list of replays to display in the GUI.
 *
 * @param scriptInterface - the Script::Interface in which to create the return data.
 * @param compareFiles - reload the cache, which takes more time,
 *                       but nearly ensures, that no changed replay is missed.
 * @return array of objects containing replay data
 */
JS::Value GetReplays(const Script::Interface& scriptInterface, bool compareFiles);

/**
 * Parses a commands.txt file and extracts metadata.
 * Works similarly to CGame::LoadReplayData().
 */
JS::Value LoadReplayData(const Script::Interface& scriptInterface, const OsPath& directory);

/**
 * Permanently deletes the visual replay (including the parent directory)
 *
 * @param replayFile - path to commands.txt, whose parent directory will be deleted.
 * @return true if deletion was successful, false on error
 */
bool DeleteReplay(const OsPath& replayFile);

/**
 * Returns the parsed header of the replay file (commands.txt).
 */
JS::Value GetReplayAttributes(const Script::Interface& scriptInterface, const OsPath& directoryName);

/**
 * Returns whether or not the metadata / summary screen data has been saved properly when the game ended.
 */
bool HasReplayMetadata(const OsPath& directoryName);

/**
 * Returns the metadata of a replay.
 */
JS::Value GetReplayMetadata(const Script::Interface& scriptInterface, const OsPath& directoryName);

/**
* Adds a replay to the replayCache.
*/
void AddReplayToCache(const Script::Interface& scriptInterface, const CStrW& directoryName);
}

#endif
