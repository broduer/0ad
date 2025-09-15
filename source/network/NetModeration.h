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

#ifndef NETMODERATION_H
#define NETMODERATION_H

/**
 * @file
 * Enumerations for the moderation messages exchanged between the clients and the server.
 */

 // Moderation actions
enum NetModerationAction
{
	NMA_LIST = 0,
	NMA_CLEAR,

	NMA_MUTE,
	NMA_UNMUTE,

	NMA_KICK,

	NMA_BAN,
	NMA_UNBAN,

	NMA_HELPER,
	NMA_UNHELPER,

	NMA_RESPONSE
};

// Moderation response types
enum NetModerationResponseCode
{
	NMRC_INVALID = 0,

	NMRC_DISALLOWED,
	NMRC_PLAYERNOTFOUND,

	NMRC_MUTED,
	NMRC_MUTE_SUCCESS,
	NMRC_UNMUTE_SUCCESS,
	NMRC_UNMUTE_NOTFOUND,

	NMRC_KICK_SUCCESS,

	NMRC_BAN_SUCCESS,
	NMRC_UNBAN_SUCCESS,
	NMRC_UNBAN_NOTFOUND,

	NMRC_HELPER_SUCCESS,
	NMRC_HELPER_NOTFOUND,
	NMRC_UNHELPER_SUCCESS,
	NMRC_UNHELPER_NOTFOUND,

	NMRC_CLEARED
};

#endif	// NETMODERATION_H
