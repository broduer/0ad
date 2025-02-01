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

#include "Globals.h"

#include "lib/code_annotation.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <cstddef>
#include "network/NetClient.h"
#include "ps/GameSetup/Config.h"
#include "ps/Input.h"
#include "soundmanager/ISoundManager.h"

bool g_app_minimized = false;
bool g_app_has_focus = true;

std::unordered_map<int32_t, bool> g_scancodes;

int g_mouse_x = 50, g_mouse_y = 50;
bool g_mouse_active = true;

// g_mouse_buttons[0] is unused. The order of entries is as in KeyName.h for MOUSE_*
bool g_mouse_buttons[MOUSE_LAST - MOUSE_BASE] = {0};

PIFrequencyFilter g_frequencyFilter;

// updates the state of the above; never swallows messages.
Input::Reaction GlobalsInputHandler(const SDL_Event& ev)
{
	size_t c;

	switch(ev.type)
	{
	case SDL_EVENT_WINDOW_MINIMIZED:
		g_app_minimized = true;
		return Input::Reaction::PASS;
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_RESTORED:
		g_app_minimized = false;
		return Input::Reaction::PASS;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		g_app_has_focus = true;
		return Input::Reaction::PASS;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		g_app_has_focus = false;
		return Input::Reaction::PASS;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
		g_mouse_active = true;
		return Input::Reaction::PASS;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		g_mouse_active = false;
		return Input::Reaction::PASS;

	case SDL_EVENT_MOUSE_MOTION:
		g_mouse_x = ev.motion.x;
		g_mouse_y = ev.motion.y;
		return Input::Reaction::PASS;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		c = ev.button.button;
		if(c < ARRAY_SIZE(g_mouse_buttons))
			g_mouse_buttons[c] = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
		else
		{
			// don't complain: just ignore people with too many mouse buttons
			//debug_warn(L"invalid mouse button");
		}
		return Input::Reaction::PASS;

	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		g_scancodes[ev.key.scancode] = (ev.type == SDL_EVENT_KEY_DOWN);
		return Input::Reaction::PASS;

	default:
		return Input::Reaction::PASS;
	}

	UNREACHABLE;
}
