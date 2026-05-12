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

#ifndef INCLUDED_GUIMANAGER
#define INCLUDED_GUIMANAGER

#include "lib/code_annotation.h"
#include "lib/input.h"
#include "lib/path.h"
#include "lib/status.h"
#include "ps/CStr.h"
#include "ps/TemplateLoader.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/StructuredClone.h"

#include <cstddef>
#include <deque>
#include <js/TypeDecls.h>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>

class CCanvas2D;
class CGUI;
class CParamNode;
namespace Script { class Context; }
namespace JS { class HandleValueArray; }
namespace JS { class Value; }
namespace PS { template <typename T, size_t N> class StaticVector; }
struct SDL_Event_;

/**
 * External interface to the GUI system.
 *
 * The GUI consists of a set of pages. Each page is constructed from a
 * series of XML files, and is independent from any other page.
 * Only one page is active at a time. All events and render requests etc
 * will go to the active page. This lets the GUI switch between pre-game menu
 * and in-game UI.
 */
class CGUIManager
{
	NONCOPYABLE(CGUIManager);
public:
	CGUIManager(Script::Context& scriptContext, Script::Interface& scriptInterface);
	~CGUIManager();

	Script::Interface& GetScriptInterface()
	{
		return m_ScriptInterface;
	}
	Script::Context& GetContext() { return m_ScriptContext; }
	std::shared_ptr<CGUI> GetActiveGUI() { return top(); }

	/**
	 * Returns the number of currently open GUI pages.
	 */
	size_t GetPageCount() const;

	/**
	 * Load a new GUI page and make it active. All current pages will be destroyed.
	 */
	void SwitchPage(const CStrW& name, const Script::Interface* srcScriptInterface, JS::HandleValue initData);

	/**
	 * Load a new GUI page and make it active. All current pages will be retained,
	 * and will still be drawn and receive tick events, but will not receive
	 * user inputs.
	 * The returned promise will be fulfilled once the pushed page is closed.
	 */
	JS::Value OpenChildPage(const CStrW& pageName, Script::StructuredClone initData);

	/**
	 * Called when a file has been modified, to hotload changes.
	 */
	Status ReloadChangedFile(const VfsPath& path);

	/**
	 * Called when we should reload all pages (e.g. translation hotloading update).
	 */
	Status ReloadAllPages();

	/**
	 * Pass input events to the currently active GUI page.
	 */
	InReaction HandleEvent(const SDL_Event_* ev);

	/**
	 * See CGUI::SendEventToAll; applies to the currently active page.
	 */
	void SendEventToAll(const CStr& eventName,
		std::optional<JS::HandleValueArray> paramData = std::nullopt) const;

	/**
	 * See CGUI::TickObjects; applies to @em all loaded pages.
	 * When the root page is closed it's returned wheter Atlas should be
	 * started.
	 */
	std::optional<bool> TickObjects();

	/**
	 * See CGUI::Draw; applies to @em all loaded pages.
	 */
	void Draw(CCanvas2D& canvas) const;

	/**
	 * See CGUI::UpdateResolution; applies to @em all loaded pages.
	 */
	void UpdateResolution();

	/**
	 * Check if a template with this name exists
	 */
	bool TemplateExists(const std::string& templateName) const;

	/**
	 * Retrieve the requested template, used for displaying faction specificities.
	 */
	const CParamNode& GetTemplate(const std::string& templateName);

	/**
	 * Display progress / description in loading screen.
	 */
	void DisplayLoadProgress(int percent, const wchar_t* pending_task);

private:
	struct SGUIPage
	{
		// COPYABLE, because event handlers may invalidate page stack iterators by open or close pages,
		// and event handlers need to be called for the entire stack.

		/**
		 * Initializes the data that will be used to create the CGUI page one or multiple times (hotloading).
		 */
		SGUIPage(const CStrW& pageName, const Script::StructuredClone initData);

		/**
		 * Create the CGUI with it's own Script::Interface. Deletes the previous CGUI if it existed.
		 */
		void LoadPage(Script::Context& context);

		/**
		 * A reference to the promise is returned. The promise will settle when the page is closed.
		 */
		JS::Value GetPromise();

		struct Close
		{
			Script::StructuredClone arg;
			bool rejected;
		};

		struct OpenRequest
		{
			std::wstring path;
			Script::StructuredClone arg;
		};

		using CloseResult = std::variant<Close, OpenRequest>;
		/**
		 * If the page should be closed this function closes the page and
		 * returns the result of the @c init function.
		 * If this page wasn't closed an empty optional is returned.
		 */
		std::optional<CloseResult> MaybeClose(const bool isRootPage);

		/**
		 * This function should be called when a child page got closed. The
		 * result of the closed page should be the argument of this
		 * function. This function resolves the @c receivingPromise.
		 */
		void Refocus(const Close& result);

		std::wstring m_Name;
		std::unordered_set<VfsPath> inputs; // for hotloading
		Script::StructuredClone initData; // data to be passed to the init() function
		std::shared_ptr<CGUI> gui; // the actual GUI page

		/**
		 * When this promise is settled this page wants to be closed. It
		 * settles with the page completion value.
		 */
		std::shared_ptr<JS::PersistentRootedObject> sendingPromise;

		/**
		 * The parent page waits on this promise. It also gets the
		 * completion value through this promise.
		 */
		std::shared_ptr<JS::PersistentRootedObject> receivingPromise;
	};

	std::shared_ptr<CGUI> top() const;

	Script::Context& m_ScriptContext;
	Script::Interface& m_ScriptInterface;

	/**
	 * The page stack must not move pointers on push/pop, or pushing a page in a page's init method
	 * may crash (as the pusher page will suddenly have moved, and the stack will be confused).
	 * Therefore use std::deque over std::vector.
	 * Also the elements have to be destructed back to front.
	 */
	class PageStackType : public std::deque<SGUIPage>
	{
	public:
		~PageStackType()
		{
			clear();
		}

		void clear()
		{
			while (!std::deque<SGUIPage>::empty())
				std::deque<SGUIPage>::pop_back();
		}
	};
	PageStackType m_PageStack;

	/**
	 * Returns an immutable copy so iterators aren't invalidated by handlers.
	 */
	PS::StaticVector<SGUIPage, 16> GetCopyOfFrozenStack() const;

	CTemplateLoader m_TemplateLoader;
};

extern CGUIManager* g_GUI;

extern InReaction gui_handler(const SDL_Event_* ev);

#endif // INCLUDED_GUIMANAGER
