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

#include "GUIManager.h"

#include "gui/CGUI.h"
#include "gui/SGUIMessage.h"
#include "lib/debug.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/file/vfs/vfs_util.h"
#include "lib/utf8.h"
#include "ps/CLogger.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/Profile.h"
#include "ps/Profiler2.h"
#include "ps/VideoMode.h"
#include "ps/XMB/XMBData.h"
#include "ps/XMB/XMBStorage.h"
#include "ps/XML/Xeromyces.h"
#include "ps/containers/StaticVector.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Context.h"
#include "scriptinterface/Conversions.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "scriptinterface/StructuredClone.h"
#include "simulation2/system/Component.h"

#include <SDL_events.h>
#include <algorithm>
#include <iterator>
#include <js/Equality.h>
#include <js/GCVector.h>
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/Symbol.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

const CStr EVENT_NAME_GAME_LOAD_PROGRESS = "GameLoadProgress";
const CStr EVENT_NAME_WINDOW_RESIZED = "WindowResized";
constexpr const char* START_ATLAS{"startAtlas"};
constexpr const char* OPEN_REQUEST{"openRequest"};

} // anonymous namespace

CGUIManager* g_GUI = nullptr;

// General TODOs:
//
// A lot of the CGUI data could (and should) be shared between
// multiple pages, instead of treating them as completely independent, to save
// memory and loading time.

// trampoline: we don't want to make the HandleEvent implementation static
Input::Reaction CGUIManager::InputHandler::operator()(const SDL_Event& ev)
{
	PROFILE("GUI event handler");
	return gui.HandleEvent(ev);
}

static Status ReloadChangedFileCB(void* param, const VfsPath& path)
{
	return static_cast<CGUIManager*>(param)->ReloadChangedFile(path);
}

CGUIManager::CGUIManager(Script::Context& scriptContext, Script::Interface& scriptInterface) :
	m_ScriptContext{scriptContext},
	m_ScriptInterface{scriptInterface},
	m_InputHandler{g_VideoMode.m_InputManager, Input::Slot::GUI, {*this}}
{
	m_ScriptInterface.SetCallbackData(this);
	m_ScriptInterface.LoadGlobalScripts();

	if (!g_Xeromyces.AddValidator(g_VFS, "gui_page", "gui/gui_page.rng"))
		LOGERROR("CGUIManager: failed to load GUI page grammar file 'gui/gui_page.rng'");
	if (!g_Xeromyces.AddValidator(g_VFS, "gui", "gui/gui.rng"))
		LOGERROR("CGUIManager: failed to load GUI XML grammar file 'gui/gui.rng'");

	RegisterFileReloadFunc(ReloadChangedFileCB, this);
}

CGUIManager::~CGUIManager()
{
	UnregisterFileReloadFunc(ReloadChangedFileCB, this);
}

size_t CGUIManager::GetPageCount() const
{
	return m_PageStack.size();
}

void CGUIManager::SwitchPage(const CStrW& pageName, const Script::Interface* srcScriptInterface, JS::HandleValue initData)
{
	// The page stack is cleared (including the script context where initData came from),
	// therefore we have to clone initData.

	Script::StructuredClone initDataClone;
	if (!initData.isUndefined())
	{
		Script::Request rq(srcScriptInterface);
		initDataClone = Script::WriteStructuredClone(rq, initData);
	}

	if (!m_PageStack.empty())
	{
		// Make sure we unfocus anything on the current page.
		m_PageStack.back().gui->SendFocusMessage(GUIM_LOST_FOCUS);
		m_PageStack.clear();
	}

	OpenChildPage(pageName, initDataClone);
}

JS::Value CGUIManager::OpenChildPage(const CStrW& pageName, Script::StructuredClone initData)
{
	// Store the callback handler in the current GUI page before opening the new one
	JS::RootedValue promise{m_ScriptInterface.GetGeneralJSContext(), [&]
		{
			if (m_PageStack.empty())
				return JS::UndefinedValue();

			CGUI& currentPage = *m_PageStack.back().gui;
			// Make sure we unfocus anything on the current page.
			currentPage.SendFocusMessage(GUIM_LOST_FOCUS);
			return m_PageStack.back().GetPromise();
		}()};

	// Emplace the page prior to loading its contents, because that may open
	// another GUI page on init which should be emplaced on top of this new page.
	m_PageStack.emplace_back(pageName, initData);
	m_PageStack.back().LoadPage(m_ScriptContext);

	return promise;
}

CGUIManager::SGUIPage::SGUIPage(const CStrW& pageName, const Script::StructuredClone initData)
	: m_Name(pageName), initData(initData)
{
}

void CGUIManager::SGUIPage::LoadPage(Script::Context& scriptContext)
{
	// If we're hotloading then try to grab some data from the previous page
	Script::StructuredClone hotloadData;
	if (gui)
	{
		std::shared_ptr<Script::Interface> scriptInterface = gui->GetScriptInterface();
		Script::Request rq(scriptInterface);
		JS::RootedValue hotloadDataVal(rq.cx, gui->GetHotloadData(rq));
		hotloadData = Script::WriteStructuredClone(rq, hotloadDataVal);
	}

	g_VideoMode.ResetCursor();
	inputs.clear();
	gui.reset(new CGUI(scriptContext));
	const Script::Request rq{gui->GetScriptInterface()};

	for (const char* name : {START_ATLAS, OPEN_REQUEST})
	{
		JS::RootedString jsName{rq.cx, JS_NewStringCopyZ(rq.cx, name)};
		JS::RootedValue symbol{rq.cx, JS::SymbolValue(JS::NewSymbol(rq.cx, jsName))};
		JS::RootedValue nativeScope{rq.cx, JS::ObjectValue(*rq.nativeScope)};
		Script::SetProperty(rq, nativeScope, name, symbol, true);
	}
	gui->AddObjectTypes();

	VfsPath path = VfsPath("gui") / m_Name;
	inputs.insert(path);

	CXeromyces xero;
	if (xero.Load(g_VFS, path, "gui_page") != PSRETURN_OK)
		// Fail silently (Xeromyces reported the error)
		return;

	int elmt_page = xero.GetElementID("page");
	int elmt_include = xero.GetElementID("include");

	XMBElement root = xero.GetRoot();

	if (root.GetNodeName() != elmt_page)
	{
		LOGERROR("GUI page '%s' must have root element <page>", utf8_from_wstring(m_Name));
		return;
	}

	VfsPath rootModule;
	for (XMBElement node : root.GetChildNodes())
	{
		if (node.GetNodeName() != elmt_include)
		{
			LOGERROR("GUI page '%s' must only have <include> elements inside <page>", utf8_from_wstring(m_Name));
			continue;
		}

		CStr8 name = node.GetText();
		CStrW nameW = node.GetText().FromUTF8();

		PROFILE2("load gui xml");
		PROFILE2_ATTR("name: %s", name.c_str());

		if (name.back() == '/')
		{
			VfsPath currentDirectory = VfsPath("gui") / nameW;
			VfsPaths directories;
			vfs::GetPathnames(g_VFS, currentDirectory, L"*.xml", directories);
			for (const VfsPath& directory : directories)
				gui->LoadXmlFile(directory, inputs);
		}
		else
		{
			VfsPath directory = VfsPath("gui") / nameW;
			gui->LoadXmlFile(directory, inputs);
		}
	}

	gui->LoadedXmlFiles();

	scriptContext.RunJobs();
	if (gui->m_LoadModuleResult.has_value())
	{
		gui->m_LoadModuleResult->moduleNamespace = gui->m_LoadModuleResult->iterator->Get();
		++gui->m_LoadModuleResult->iterator;
	}

	JS::RootedValue hotloadDataVal(rq.cx);

	if (hotloadData)
		Script::ReadStructuredClone(rq, hotloadData, &hotloadDataVal);

	sendingPromise = std::make_shared<JS::PersistentRootedObject>(rq.cx);
	// Assigning to `sendingPromise` isn't possible after `init` has been called because `init` might
	// replace this page. So a local copy has to be made.
	const std::shared_ptr<JS::PersistentRootedObject> localPromise{sendingPromise};

	JS::RootedObject returnObject{rq.cx, gui->CallPageInit(rq, initData, hotloadDataVal,
		utf8_from_wstring(m_Name))};

	*localPromise = returnObject ? returnObject : JS::NewPromiseObject(rq.cx, nullptr);
}

JS::Value CGUIManager::SGUIPage::GetPromise()
{
	const Script::Request rq{gui->GetScriptInterface()};
	if (receivingPromise == nullptr)
	{
		receivingPromise = std::make_shared<JS::PersistentRootedObject>(rq.cx,
			JS::NewPromiseObject(rq.cx, nullptr));
	}

	return JS::ObjectValue(**receivingPromise);
}

std::optional<CGUIManager::SGUIPage::CloseResult> CGUIManager::SGUIPage::MaybeClose(const bool isRootPage)
{
	if (JS::GetPromiseState(*sendingPromise) == JS::PromiseState::Pending)
		return std::nullopt;

	// Make sure we unfocus anything on the current page.
	gui->SendFocusMessage(GUIM_LOST_FOCUS);

	const Script::Request rq{gui->GetScriptInterface()};
	JS::RootedValue arg{rq.cx, JS::GetPromiseResult(*sendingPromise)};
	const bool rejected{JS::GetPromiseState(*sendingPromise) == JS::PromiseState::Rejected};

	JS::RootedValue nativeScope{rq.cx, JS::ObjectValue(*rq.nativeScope)};
	if (arg.isObject())
	{
		JS::RootedObject argObject{rq.cx, &arg.toObject()};
		JS::RootedValue symbol{rq.cx};
		Script::GetProperty(rq, nativeScope, OPEN_REQUEST, &symbol);
		JS::RootedId key{rq.cx, JS::PropertyKey::Symbol(symbol.toSymbol())};
		JS::RootedValue openRequest{rq.cx};
		if (JS_GetPropertyById(rq.cx, argObject, key, &openRequest) &&
			Script::HasProperty(rq, openRequest, "page"))
		{
			std::wstring page;
			Script::GetProperty(rq, openRequest, "page", page);
			JS::RootedValue openArg{rq.cx};
			Script::GetProperty(rq, openRequest, "argument", &openArg);
			return CGUIManager::SGUIPage::OpenRequest{page,
				Script::WriteStructuredClone(rq, openArg)};
		}
	}

	if (isRootPage)
	{
		JS::RootedValue symbol{rq.cx};
		Script::GetProperty(rq, nativeScope, START_ATLAS, &symbol);
		bool equals;
		if (!JS::StrictlyEqual(rq.cx, arg, symbol, &equals))
			throw std::runtime_error{"Error while comparing return value to a symbol."};

		if (equals)
			return CGUIManager::SGUIPage::Close{nullptr, rejected};
	}
	return CGUIManager::SGUIPage::Close{Script::WriteStructuredClone(rq, arg), rejected};
}

void CGUIManager::SGUIPage::Refocus(const Close& result)
{
	ENSURE(receivingPromise);

	std::shared_ptr<Script::Interface> scriptInterface = gui->GetScriptInterface();
	Script::Request rq(scriptInterface);

	JS::RootedObject globalObj(rq.cx, rq.glob);

	JS::RootedObject recv(rq.cx, *std::exchange(receivingPromise, nullptr));

	JS::RootedValue argVal(rq.cx);
	Script::ReadStructuredClone(rq, result.arg, &argVal);

	// This only resolves the promise, it doesn't call the continuation.
	(result.rejected ? JS::RejectPromise : JS::ResolvePromise)(rq.cx, recv, argVal);

	// We return to a page where some object might have been focused.
	gui->SendFocusMessage(GUIM_GOT_FOCUS);
}

Status CGUIManager::ReloadChangedFile(const VfsPath& path)
{
	for (SGUIPage& p : m_PageStack)
		if (p.inputs.find(path) != p.inputs.end())
		{
			LOGMESSAGE("GUI file '%s' changed - reloading page '%s'", path.string8(), utf8_from_wstring(p.m_Name));
			p.LoadPage(m_ScriptContext);
			// TODO: this can crash if LoadPage runs an init script which modifies the page stack and breaks our iterators
		}

	return INFO::OK;
}

Status CGUIManager::ReloadAllPages()
{
	// TODO: this can crash if LoadPage runs an init script which modifies the page stack and breaks our iterators
	for (SGUIPage& p : m_PageStack)
		p.LoadPage(m_ScriptContext);

	return INFO::OK;
}

Input::Reaction CGUIManager::HandleEvent(const SDL_Event& ev)
{
	// We want scripts to have access to the raw input events, so they can do complex
	// processing when necessary (e.g. for unit selection and camera movement).
	// Sometimes they'll want to be layered behind the GUI widgets (e.g. to detect mousedowns on the
	// visible game area), sometimes they'll want to intercepts events before the GUI (e.g.
	// to capture all mouse events until a mouseup after dragging).
	// So we call two separate handler functions:

	bool handled = false;

	{
		PROFILE("handleInputBeforeGui");
		Script::Request rq(*top()->GetScriptInterface());

		JS::RootedValue global(rq.cx, rq.globalValue());
		if (Script::Function::Call(rq, global, "handleInputBeforeGui", handled, ev, top()->FindObjectUnderMouse()))
			if (handled)
				return Input::Reaction::HANDLED;
	}

	{
		PROFILE("handle event in native GUI");
		Input::Reaction r = top()->HandleEvent(ev);
		if (r != Input::Reaction::PASS)
			return r;
	}

	{
		// We can't take the following lines out of this scope because top() may be another gui page than it was when calling handleInputBeforeGui!
		Script::Request rq(*top()->GetScriptInterface());
		JS::RootedValue global(rq.cx, rq.globalValue());

		PROFILE("handleInputAfterGui");
		if (Script::Function::Call(rq, global, "handleInputAfterGui", handled, ev))
			if (handled)
				return Input::Reaction::HANDLED;
	}

	return Input::Reaction::PASS;
}


void CGUIManager::SendEventToAll(const CStr& eventName,
	std::optional<JS::HandleValueArray> paramData) const
{
	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
	{
		if (paramData.has_value())
			p.gui->SendEventToAll(eventName, paramData.value());
		else
			p.gui->SendEventToAll(eventName);
	}
}

std::optional<bool> CGUIManager::TickObjects(const float realTimeSinceLastFrame)
{
	PROFILE3("gui tick");

	// We share the script context with everything else that runs in the same thread.
	// This call makes sure we trigger GC regularly even if the simulation is not running.
	m_ScriptContext.MaybeIncrementalGC();

	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
	{
		const Script::Request rq{p.gui->GetScriptInterface()};
		JS::RootedObject newSendingPromise{rq.cx, p.gui->TickObjects(rq, p.initData,
			utf8_from_wstring(p.m_Name), realTimeSinceLastFrame)};
		if (newSendingPromise)
			(*p.sendingPromise) = newSendingPromise;
	}

	m_ScriptContext.RunJobs();

	while (!m_PageStack.empty())
	{
		const size_t stackSize{m_PageStack.size()};
		const std::optional<SGUIPage::CloseResult> result{
			m_PageStack.back().MaybeClose(stackSize == 1)};
		if (!result.has_value())
			break;
		ENSURE(m_PageStack.size() == stackSize);
		m_PageStack.pop_back();
		if (const SGUIPage::OpenRequest* request{std::get_if<SGUIPage::OpenRequest>(&result.value())})
			OpenChildPage(request->path, request->arg);
		else if (const SGUIPage::Close& ret{std::get<SGUIPage::Close>(result.value())};
			m_PageStack.empty())
		{
			return !ret.arg;
		}
		else
			m_PageStack.back().Refocus(ret);

		m_ScriptContext.RunJobs();
	}
	return std::nullopt;
}

void CGUIManager::Draw(CCanvas2D& canvas) const
{
	PROFILE3("gui");

	for (const SGUIPage& p : m_PageStack)
		p.gui->Draw(canvas);
}

void CGUIManager::UpdateResolution()
{
	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
	{
		p.gui->UpdateResolution();
		p.gui->SendEventToAll(EVENT_NAME_WINDOW_RESIZED);
	}
}

bool CGUIManager::TemplateExists(const std::string& templateName) const
{
	return m_TemplateLoader.TemplateExists(templateName);
}

const CParamNode& CGUIManager::GetTemplate(const std::string& templateName)
{
	const CParamNode& templateRoot = m_TemplateLoader.GetTemplateFileData(templateName).GetOnlyChild();
	if (!templateRoot.IsOk())
		LOGERROR("Invalid template found for '%s'", templateName.c_str());

	return templateRoot;
}

void CGUIManager::DisplayLoadProgress(int percent, const wchar_t* pending_task)
{
	const Script::Interface& scriptInterface = *(GetActiveGUI()->GetScriptInterface());
	Script::Request rq(scriptInterface);

	JS::RootedValueVector paramData(rq.cx);

	std::ignore = paramData.append(JS::NumberValue(percent));

	JS::RootedValue valPendingTask(rq.cx);
	Script::ToJSVal(rq, &valPendingTask, pending_task);
	std::ignore = paramData.append(valPendingTask);

	SendEventToAll(EVENT_NAME_GAME_LOAD_PROGRESS, paramData);
}

// This returns a shared_ptr to make sure the CGUI doesn't get deallocated
// while we're in the middle of calling a function on it (e.g. if a GUI script
// calls SwitchPage)
std::shared_ptr<CGUI> CGUIManager::top() const
{
	ENSURE(m_PageStack.size());
	return m_PageStack.back().gui;
}

PS::StaticVector<CGUIManager::SGUIPage, 16> CGUIManager::GetCopyOfFrozenStack() const
{
	PS::StaticVector<CGUIManager::SGUIPage, 16> stack;
	std::copy(m_PageStack.begin(), m_PageStack.end(), std::back_inserter(stack));
	return stack;
}
