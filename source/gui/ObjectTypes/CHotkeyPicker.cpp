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

#include "CHotkeyPicker.h"

#include "gui/CGUI.h"
#include "gui/ObjectBases/IGUIObject.h"
#include "gui/SGUIMessage.h"
#include "lib/timer.h"
#include "ps/CLogger.h"
#include "ps/KeyName.h"
#include "scriptinterface/Conversions.h"
#include "scriptinterface/Request.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <memory>


const CStr CHotkeyPicker::EventNameCombination = "Combination";
const CStr CHotkeyPicker::EventNameKeyChange = "KeyChange";

// Don't send the scancode, JS doesn't care.
template<> void Script::ToJSVal(const Script::Request& rq, JS::MutableHandleValue ret, const CHotkeyPicker::Key& val)
{
	Script::ToJSVal(rq, ret, val.scancodeName);
}

// Unused, but JSVAL_VECTOR requires it.
template<> bool Script::FromJSVal(const Script::Request&, const JS::HandleValue, CHotkeyPicker::Key&)
{
	LOGWARNING("FromJSVal<CHotkeyPicker>: Not implemented");
	return false;
}

JSVAL_VECTOR(CHotkeyPicker::Key);

CHotkeyPicker::CHotkeyPicker(CGUI& pGUI) : IGUIObject(pGUI), m_TimeToCombination(this, "time_to_combination", 1.f)
{
	// 8 keys at the same time is probably more than we'll ever need.
	m_KeysPressed.reserve(8);
}

void CHotkeyPicker::FireEvent(const CStr& event)
{
	Script::Request rq(*m_pGUI.GetScriptInterface());

	JS::RootedValueArray<1> args(rq.cx);
	JS::RootedValue keys(rq.cx);
	Script::ToJSVal(rq, &keys, m_KeysPressed);
	args[0].set(keys);
	ScriptEvent(event, args);
}

void CHotkeyPicker::Tick()
{
	if (m_KeysPressed.size() == 0)
		return;

	double time = timer_Time();
	if (time - m_LastKeyChange < m_TimeToCombination)
		return;

	FireEvent(EventNameCombination);

	return;
}

void CHotkeyPicker::HandleMessage(SGUIMessage& Message)
{
	IGUIObject::HandleMessage(Message);
	switch (Message.type)
	{
	case GUIM_GOT_FOCUS:
	case GUIM_LOST_FOCUS:
	{
		m_KeysPressed.clear();
		m_LastKeyChange = timer_Time();
		break;
	}
	default:
		break;
	}
}

Input::Reaction CHotkeyPicker::PreemptEvent(const SDL_Event& ev)
{
	switch (ev.type)
	{
	// Handle the same mouse events that hotkeys handle
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_MOUSE_WHEEL:
	{
		SDL_Scancode scancode;

		if (ev.type != SDL_EVENT_MOUSE_WHEEL)
		{
			// Wait a little bit -> this gets triggered when clicking on a button,
			// but after the button click is processed, thus immediately triggering...
			if (timer_Time()-m_LastKeyChange < 0.2)
				return Input::Reaction::HANDLED;
			// This is from hotkeyHandler - not sure what it does in all honesty.
			if(ev.button.button >= SDL_BUTTON_X1)
				scancode = static_cast<SDL_Scancode>(MOUSE_BASE + static_cast<int>(ev.button.button) + 2);
			else
				scancode = static_cast<SDL_Scancode>(MOUSE_BASE + static_cast<int>(ev.button.button));
		}
		else
		{
			if (ev.wheel.y > 0)
				scancode = static_cast<SDL_Scancode>(MOUSE_WHEELUP);
			else if (ev.wheel.y < 0)
				scancode = static_cast<SDL_Scancode>(MOUSE_WHEELDOWN);
			else if (ev.wheel.x > 0)
				scancode = static_cast<SDL_Scancode>(MOUSE_X2);
			else if (ev.wheel.x < 0)
				scancode = static_cast<SDL_Scancode>(MOUSE_X1);
			else
				return Input::Reaction::HANDLED;
		}
		// Don't handle keys and mouse together except for modifiers.
		m_KeysPressed.erase(std::remove_if(m_KeysPressed.begin(), m_KeysPressed.end(), [](const Key& k) {
			return static_cast<int>(k.code) < UNIFIED_SHIFT || static_cast<int>(k.code) >= UNIFIED_LAST; } ), m_KeysPressed.end());
		m_KeysPressed.emplace_back(Key{scancode, FindScancodeName(scancode)});
		// For mouse events, assume we immediately want to return.
		FireEvent(EventNameCombination);

		return Input::Reaction::HANDLED;
	}
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	{
		SDL_Scancode scancode = ev.key.scancode;

		// Don't handle caps-lock, it doesn't really work in-game and it's a weird hotkey.
		if (scancode == SDL_SCANCODE_CAPSLOCK)
			return Input::Reaction::PASS;

		if (scancode == SDL_SCANCODE_LSHIFT || scancode == SDL_SCANCODE_RSHIFT)
			scancode = static_cast<SDL_Scancode>(UNIFIED_SHIFT);
		else if (scancode == SDL_SCANCODE_LCTRL || scancode == SDL_SCANCODE_RCTRL)
			scancode = static_cast<SDL_Scancode>(UNIFIED_CTRL);
		else if (scancode == SDL_SCANCODE_LALT || scancode == SDL_SCANCODE_RALT)
			scancode = static_cast<SDL_Scancode>(UNIFIED_ALT);
		else if (scancode == SDL_SCANCODE_LGUI || scancode == SDL_SCANCODE_RGUI)
			scancode = static_cast<SDL_Scancode>(UNIFIED_SUPER);

		if (ev.type == SDL_EVENT_KEY_DOWN)
		{
			std::vector<Key>::const_iterator it = \
				std::find_if(m_KeysPressed.begin(), m_KeysPressed.end(), [&scancode](Key& k) { return k.code == scancode; });
			// Can happen if multiple keys are mapped the same.
			if (it != m_KeysPressed.end())
				return Input::Reaction::HANDLED;
			m_KeysPressed.emplace_back(Key{scancode, FindScancodeName(scancode)});
		}
		else
		{
			std::vector<Key>::const_iterator it = \
				std::find_if(m_KeysPressed.begin(), m_KeysPressed.end(), [&scancode](Key& k) { return k.code == scancode; });
			// Might happen if a key was down before this object is created.
			if (it == m_KeysPressed.end())
				return Input::Reaction::HANDLED;
			m_KeysPressed.erase(it);
		}

		FireEvent(EventNameKeyChange);

		// Register after-JS in case this takes a while (probably not but it doesn't hurt).
		m_LastKeyChange = timer_Time();
		return Input::Reaction::HANDLED;
	}
	default:
	{
		return Input::Reaction::PASS;
	}
	}
}
