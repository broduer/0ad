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

#include "lib/self_test.h"

#include "gui/GUIManager.h"

#include "gui/CGUI.h"
#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/GameSetup/GameSetup.h"
#include "ps/Hotkey.h"
#include "ps/VideoMode.h"
#include "ps/XML/Xeromyces.h"
#include "ps/VideoMode.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Conversions.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "scriptinterface/StructuredClone.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <array>
#include <js/CallArgs.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

#include "js/Promise.h"

class TestGuiManager : public CxxTest::TestSuite
{
	std::unique_ptr<CConfigDB> configDB;
	std::optional<CXeromycesEngine> xeromycesEngine;
	std::optional<Script::Interface> scriptInterface;
public:

	void setUp()
	{
		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "_test.gui" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));

		configDB = std::make_unique<CConfigDB>();

		xeromycesEngine.emplace();

		scriptInterface.emplace("Engine", "GUIManager", *g_ScriptContext);
		g_GUI = new CGUIManager{*g_ScriptContext, *scriptInterface};
	}

	void tearDown()
	{
		delete g_GUI;
		scriptInterface.reset();
		xeromycesEngine.reset();
		configDB.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir()/"_testcache");
	}

	void test_EventObject()
	{
		// Load up a test page.
		Script::Request rq{g_GUI->GetScriptInterface()};
		JS::RootedValue val(rq.cx);
		Script::CreateObject(rq, &val);

		Script::StructuredClone data = Script::WriteStructuredClone(rq, JS::NullHandleValue);
		g_GUI->OpenChildPage(L"event/page_event.xml", data);

		const Script::Interface& pageScriptInterface = *(g_GUI->GetActiveGUI()->GetScriptInterface());
		Script::Request prq(pageScriptInterface);
		JS::RootedValue global(prq.cx, prq.globalValue());

		int called_value = 0;
		JS::RootedValue js_called_value(prq.cx);

		// Ticking will call the onTick handlers of all object. The second
		// onTick is configured to disable the onTick handlers of the first and
		// third and enable the fourth. So ticking once will only call the
		// first and second object. We don't want the fourth object to be
		// called, to avoid infinite additions of objects.
		g_GUI->TickObjects();
		Script::GetProperty(prq, global, "called1", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 1);

		Script::GetProperty(prq, global, "called2", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 1);

		Script::GetProperty(prq, global, "called3", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 0);

		Script::GetProperty(prq, global, "called4", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 0);

		// Ticking again will still call the second object, but also the fourth.
		g_GUI->TickObjects();
		Script::GetProperty(prq, global, "called1", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 1);

		Script::GetProperty(prq, global, "called2", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 2);

		Script::GetProperty(prq, global, "called3", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 0);

		Script::GetProperty(prq, global, "called4", &js_called_value);
		Script::FromJSVal(prq, js_called_value, called_value);
		TS_ASSERT_EQUALS(called_value, 1);
	}

	void test_hotkeysState()
	{
		// Load up a fake test hotkey when pressing 'a'.
		const char* test_hotkey_name = "hotkey.test";
		configDB->SetValueString(CFG_SYSTEM, test_hotkey_name, "A");
		LoadHotkeys(*configDB);

		// Load up a test page.
		Script::Request rq{g_GUI->GetScriptInterface()};
		JS::RootedValue val(rq.cx);
		Script::CreateObject(rq, &val);

		Script::StructuredClone data = Script::WriteStructuredClone(rq, JS::NullHandleValue);
		g_GUI->OpenChildPage(L"hotkey/page_hotkey.xml", data);

		// Press 'a'.
		SDL_Event hotkeyNotification;
		hotkeyNotification.type = SDL_EVENT_KEY_DOWN;
		hotkeyNotification.key.scancode = SDL_SCANCODE_A;
		hotkeyNotification.key.repeat = 0;

		// Init input and poll the event.
		const std::unique_ptr<InputHandlers> _{MakeInputHandlers()};
		g_VideoMode.m_InputManager.PushPriorityEvent(hotkeyNotification);
		for (SDL_Event& ev : g_VideoMode.m_InputManager.PollEvents())
			g_VideoMode.m_InputManager.DispatchEvent(ev);

		const Script::Interface& pageScriptInterface = *(g_GUI->GetActiveGUI()->GetScriptInterface());
		Script::Request prq(pageScriptInterface);
		JS::RootedValue global(prq.cx, prq.globalValue());

		// Ensure that our hotkey state was synchronised with the event itself.
		bool hotkey_pressed_value = false;
		JS::RootedValue js_hotkey_pressed_value(prq.cx);

		Script::GetProperty(prq, global, "state_before", &js_hotkey_pressed_value);
		Script::FromJSVal(prq, js_hotkey_pressed_value, hotkey_pressed_value);
		TS_ASSERT_EQUALS(hotkey_pressed_value, true);

		hotkey_pressed_value = false;
		Script::GetProperty(prq, global, "state_after", &js_hotkey_pressed_value);
		Script::FromJSVal(prq, js_hotkey_pressed_value, hotkey_pressed_value);
		TS_ASSERT_EQUALS(hotkey_pressed_value, true);

		// We are listening to KeyDown events, so repeat shouldn't matter.
		hotkeyNotification.key.repeat = 1;
		g_VideoMode.m_InputManager.PushPriorityEvent(hotkeyNotification);
		for (SDL_Event& ev : g_VideoMode.m_InputManager.PollEvents())
			g_VideoMode.m_InputManager.DispatchEvent(ev);

		hotkey_pressed_value = false;
		Script::GetProperty(prq, global, "state_before", &js_hotkey_pressed_value);
		Script::FromJSVal(prq, js_hotkey_pressed_value, hotkey_pressed_value);
		TS_ASSERT_EQUALS(hotkey_pressed_value, true);

		hotkey_pressed_value = false;
		Script::GetProperty(prq, global, "state_after", &js_hotkey_pressed_value);
		Script::FromJSVal(prq, js_hotkey_pressed_value, hotkey_pressed_value);
		TS_ASSERT_EQUALS(hotkey_pressed_value, true);

		hotkeyNotification.type = SDL_EVENT_KEY_UP;
		g_VideoMode.m_InputManager.PushPriorityEvent(hotkeyNotification);
		for (SDL_Event& ev : g_VideoMode.m_InputManager.PollEvents())
			g_VideoMode.m_InputManager.DispatchEvent(ev);

		hotkey_pressed_value = true;
		Script::GetProperty(prq, global, "state_before", &js_hotkey_pressed_value);
		Script::FromJSVal(prq, js_hotkey_pressed_value, hotkey_pressed_value);
		TS_ASSERT_EQUALS(hotkey_pressed_value, false);

		hotkey_pressed_value = true;
		Script::GetProperty(prq, global, "state_after", &js_hotkey_pressed_value);
		Script::FromJSVal(prq, js_hotkey_pressed_value, hotkey_pressed_value);
		TS_ASSERT_EQUALS(hotkey_pressed_value, false);

		UnloadHotkeys();
	}

	static void CloseTopmostPage()
	{
		Script::Request rq{g_GUI->GetActiveGUI()->GetScriptInterface()};
		JS::RootedValue global{rq.cx, rq.globalValue()};
		TS_ASSERT(Script::Function::CallVoid(rq, global, "closePageCallback"));
		// Check whether promises are settled in the page stack and flush the stack.
		g_GUI->TickObjects();
	}

	void test_PageRegainedFocusEvent()
	{
		Script::Request rq{g_GUI->GetScriptInterface()};
		const Script::StructuredClone undefined{
			Script::WriteStructuredClone(rq, JS::UndefinedHandleValue)};

		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 0);
		g_GUI->OpenChildPage(L"regainFocus/page_emptyPage.xml", undefined);
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 1);

		// This page instantly pushes an empty page with a callback that pops another page again.
		g_GUI->OpenChildPage(L"regainFocus/page_pushWithPopOnInit.xml", undefined);
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 3);

		// Pop the empty page and execute the continuation.
		CloseTopmostPage();
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 1);

		CloseTopmostPage();
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 0);
	}

	void test_ResolveReject()
	{
		TestLogger logger;
		constexpr std::array<std::tuple<bool, JS::PromiseState>, 2> testSteps{{
			{false, JS::PromiseState::Fulfilled},
			{true, JS::PromiseState::Rejected}}};

		const Script::Request rq{g_GUI->GetScriptInterface()};

		const Script::StructuredClone undefined{
			Script::WriteStructuredClone(rq, JS::UndefinedHandleValue)};
		g_GUI->OpenChildPage(L"regainFocus/page_emptyPage.xml", undefined);


		for (const auto& [reject, result] : testSteps)
		{
			const JS::RootedValue value{rq.cx, JS::BooleanValue(reject)};
			const Script::StructuredClone clonedValue{Script::WriteStructuredClone(rq, value)};

			const JS::RootedValue promise{rq.cx,
				g_GUI->OpenChildPage(L"resolveReject/page_resolveReject.xml", clonedValue)};

			// Check whether promises are settled in the page stack and flush the stack.
			g_GUI->TickObjects();
			const JS::RootedObject promiseObject{rq.cx, &promise.toObject()};
			TS_ASSERT_EQUALS(JS::GetPromiseState(promiseObject), result);
		}
	}

	void test_Sequential()
	{
		const Script::Request rq{g_GUI->GetScriptInterface()};
		const Script::StructuredClone undefined{
			Script::WriteStructuredClone(rq, JS::UndefinedHandleValue)};
		g_GUI->OpenChildPage(L"sequential/page_sequential.xml", undefined);
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 2);
		CloseTopmostPage();
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 2);
		CloseTopmostPage();
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 0);
	}

	void test_Result()
	{
		const Script::Request rq{g_GUI->GetScriptInterface()};
		g_GUI->OpenChildPage(L"Result/page_Result.xml",
			Script::WriteStructuredClone(rq, JS::FalseHandleValue));
		TS_ASSERT(!g_GUI->TickObjects().value());

		g_GUI->OpenChildPage(L"Result/page_Result.xml",
			Script::WriteStructuredClone(rq, JS::TrueHandleValue));
		TS_ASSERT(g_GUI->TickObjects().value());
	}

	void test_MultipleRootModules()
	{
		Script::Request rq{g_GUI->GetScriptInterface()};

		TS_ASSERT_THROWS_EQUALS(g_GUI->OpenChildPage(
			L"multiple_root-modules/page.xml",
			Script::WriteStructuredClone(rq, JS::NullHandleValue)),
			const std::logic_error& e, e.what(), "There can only be one root module per page.");
	}

	void test_Await()
	{
		Script::Request rq{g_GUI->GetScriptInterface()};

		TS_ASSERT_THROWS(g_GUI->OpenChildPage(L"await/page.xml",
			Script::WriteStructuredClone(rq, JS::NullHandleValue)), const std::bad_variant_access&);
	}

	void test_OpenRequest()
	{
		const Script::Request rq{g_GUI->GetScriptInterface()};
		g_GUI->OpenChildPage(L"OpenRequest/Root/Page.xml",
			Script::WriteStructuredClone(rq, JS::UndefinedHandleValue));
		TS_ASSERT_EQUALS(g_GUI->GetPageCount(), 2);

		g_GUI->TickObjects();

		const Script::Request pageRq{g_GUI->GetActiveGUI()->GetScriptInterface()};
		JS::RootedValue global{pageRq.cx, pageRq.globalValue()};
		std::string result;
		TS_ASSERT(Script::Function::Call(pageRq, global, "closePageCallback", result));

		TS_ASSERT_STR_EQUALS(result, "Entry Continuation");
	}
};
