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

#include "ICmpCinemaManager.h"

#include "simulation2/helpers/CinemaPath.h"
#include "simulation2/system/InterfaceScripted.h"

BEGIN_INTERFACE_WRAPPER(CinemaManager)
DEFINE_INTERFACE_METHOD("AddPath", ICmpCinemaManager, AddPath)
DEFINE_INTERFACE_METHOD("HasPath", ICmpCinemaManager, HasPath)
DEFINE_INTERFACE_METHOD("DeletePath", ICmpCinemaManager, DeletePath)
DEFINE_INTERFACE_METHOD("PushPathToQueue", ICmpCinemaManager, PushPathToQueue)
DEFINE_INTERFACE_METHOD("ClearQueue", ICmpCinemaManager, ClearQueue)
DEFINE_INTERFACE_METHOD("StartPlayingQueue", ICmpCinemaManager, StartPlayingQueue)
DEFINE_INTERFACE_METHOD("IsPlayingQueue", ICmpCinemaManager, IsPlayingQueue)
DEFINE_INTERFACE_METHOD("GetActivePath", ICmpCinemaManager, GetActivePath)
DEFINE_INTERFACE_METHOD("GetActivePathElapsedTime", ICmpCinemaManager, GetActivePathElapsedTime)
DEFINE_INTERFACE_METHOD("StopPlayingQueue", ICmpCinemaManager, StopPlayingQueue)
END_INTERFACE_WRAPPER(CinemaManager)
