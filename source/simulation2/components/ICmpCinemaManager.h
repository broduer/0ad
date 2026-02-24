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

#ifndef INCLUDED_ICMPCINEMAMANAGER
#define INCLUDED_ICMPCINEMAMANAGER


#include "simulation2/system/Component.h"
#include "simulation2/system/Interface.h"

#include <js/Value.h>
#include <list>
#include <map>

class CCamera;
class CCinemaPath;
class CStrW;

/**
 * Manages a dynamic list of cinema paths (predefined camera movements/cutscenes) as well as a queue of paths that
 * is played one by one on command to all players. Simulation messages are sent each time a path ends and also when
 * the queue finishes as a whole.
 */
class ICmpCinemaManager : public IComponent
{
public:
	/**
	* Register a new path.
	* @param path path data
	*/
	virtual void AddPath(const CCinemaPath& path) = 0;

	/**
	 * Remove a path and its data entirely.
	 * @param name path name
	 */
	virtual void DeletePath(const CStrW& name) = 0;

	/**
	* Check whether a path exists (is registered under the given name)..
	* @param name path name
	*/
	virtual bool HasPath(const CStrW& name) const = 0;

	/**
	 * Get all registered paths, keyed by their names.
	 */
	virtual const std::map<CStrW, CCinemaPath>& GetPaths() const = 0;

	/**
	 * Override the entire list of existing paths.
	 * @param newPaths new list of paths
	 */
	virtual void SetPaths(const std::map<CStrW, CCinemaPath>& newPaths) = 0;

	/**
	* Push a path to the back of the queue.
	* @param name path name
	*/
	virtual void PushPathToQueue(const CStrW& name) = 0;

	/**
	* Clear all paths from the queue.
	*/
	virtual void ClearQueue() = 0;

	/**
	 * Start playing the paths in the queue one by one.
	 */
	virtual void StartPlayingQueue() = 0;

	/**
	 * Stop playing the active path and the queue altogether.
	 */
	virtual void StopPlayingQueue() = 0;

	/**
	 * Whether the first path of the queue is being played at the moment.
	 */
	virtual bool IsPlayingQueue() const = 0;

	/**
	 * Send an update to the path currently playing for it to determine the new camera position.
	 * Called every frame.
	 */
	virtual void UpdateActivePath(const float deltaRealTime, CCamera* camera) = 0;

	/**
	 * Get the name of the path currently playing, if any.
	 */
	virtual const CStrW GetActivePath() const = 0;

	/**
	 * Get the time elapsed since the currently active path started playing.
	 */
	virtual const fixed GetActivePathElapsedTime() const = 0;

	DECLARE_INTERFACE_TYPE(CinemaManager)
};

#endif // INCLUDED_ICMPCINEMAMANAGER
