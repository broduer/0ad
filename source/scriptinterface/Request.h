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

#ifndef INCLUDED_SCRIPT_REQUEST
#define INCLUDED_SCRIPT_REQUEST

#include "scriptinterface/ScriptForward.h"

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <memory>

namespace JS { class Realm; }
namespace JS { class Value; }
namespace Script { class Interface; }
struct JSContext;

/**
 * Spidermonkey maintains some 'local' state via the JSContext* object.
 * This object is an argument to most JSAPI functions.
 * Furthermore, this state is Realm (~ global) dependent. For many reasons, including GC safety,
 * The JSContext* Realm must be set up correctly when accessing it.
 * 'Entering' and 'Leaving' realms must be done in a LIFO manner.
 * SM recommends using JSAutoRealm, which provides an RAII option.
 *
 * Script::Request combines both of the above in a single convenient package,
 * providing safe access to the JSContext*, the global object, and ensuring that the proper realm has been entered.
 * Most scriptinterface/ functions will take a Script::Request, to ensure proper rooting. You may sometimes
 * have to create one from a Script::Interface.
 *
 * Be particularly careful when manipulating several script interfaces.
 */
namespace Script
{

class Request
{
	Request() = delete;
	Request(const Script::Request& rq) = delete;
	Request& operator=(const Script::Request& rq) = delete;
public:
	/**
	 * NB: the definitions are in scriptinterface.cpp, because these access members of the PImpled
	 * implementation of Script::Interface, and that seemed more convenient.
	 */
	Request(const Script::Interface& scriptInterface);
	Request(const Script::Interface* scriptInterface) : Request(*scriptInterface) {}
	Request(std::shared_ptr<Script::Interface> scriptInterface) : Request(*scriptInterface) {}
	~Request();

	/**
	 * Create a script request from a JSContext.
	 * This can be used to get the script interface in a JSNative function.
	 * In general, you shouldn't have to rely on this otherwise.
	 */
	Request(JSContext* cx);

	/**
	 * Return the scriptInterface active when creating this Script::Request.
	 * Note that this is multi-request safe: even if another Script::Request is created,
	 * it will point to the original scriptInterface, and thus can be used to re-enter the realm.
	 */
	const Script::Interface& GetScriptInterface() const;

	JS::Value globalValue() const;

	// Note that JSContext actually changes behind the scenes when creating another Script::Request for another realm,
	// so be _very_ careful when juggling between different realms.
	JSContext* cx;
	JS::HandleObject glob;
	JS::HandleObject nativeScope;
private:
	const Script::Interface& m_ScriptInterface;
	JS::Realm* m_FormerRealm;
};

}

#endif // INCLUDED_SCRIPT_REQUEST
