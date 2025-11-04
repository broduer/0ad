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

#ifndef INCLUDED_SCRIPTREQUEST
#define INCLUDED_SCRIPTREQUEST

#include "scriptinterface/ScriptForward.h"

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <memory>
#include <optional>

class ScriptInterface;
namespace JS { class Realm; }
namespace JS { class Value; }
struct JSContext;

/**
 * Spidermonkey maintains some 'local' state via the JSContext* object.
 * This object is an argument to most JSAPI functions.
 * Furthermore, this state is Realm (~ global) dependent. For many reasons, including GC safety,
 * The JSContext* Realm must be set up correctly when accessing it.
 * 'Entering' and 'Leaving' realms must be done in a LIFO manner.
 * SM recommends using JSAutoRealm, which provides an RAII option.
 *
 * ScriptRequest combines both of the above in a single convenient package,
 * providing safe access to the JSContext*, the global object, and ensuring that the proper realm has been entered.
 * Most scriptinterface/ functions will take a ScriptRequest, to ensure proper rooting. You may sometimes
 * have to create one from a ScriptInterface.
 *
 * Be particularly careful when manipulating several script interfaces.
 */
class ScriptRequest
{
	ScriptRequest() = delete;
	ScriptRequest(const ScriptRequest& rq) = delete;
	ScriptRequest& operator=(const ScriptRequest& rq) = delete;
public:
	static void PrintPointers();

	/**
	 * NB: the definitions are in scriptinterface.cpp, because these access members of the PImpled
	 * implementation of ScriptInterface, and that seemed more convenient.
	 */
	ScriptRequest(const ScriptInterface& scriptInterface);
	ScriptRequest(const ScriptInterface* scriptInterface) : ScriptRequest(*scriptInterface) {}
	ScriptRequest(std::shared_ptr<ScriptInterface> scriptInterface) : ScriptRequest(*scriptInterface) {}
	ScriptRequest(ScriptRequest &&scriptRequest);
	~ScriptRequest();

	/**
	 * Create a script request from a JSContext.
	 * This can be used to get the script interface in a JSNative function.
	 * In general, you shouldn't have to rely on this otherwise.
	 */
	ScriptRequest(JSContext* cx);

	/**
	 * Return the scriptInterface active when creating this ScriptRequest.
	 * Note that this is multi-request safe: even if another ScriptRequest is created,
	 * it will point to the original scriptInterface, and thus can be used to re-enter the realm.
	 */
	const ScriptInterface& GetScriptInterface() const;

	JS::Value globalValue() const;

	template<typename T>
	class PtrHolder
	{
	public:
		template<typename ...Args>
		PtrHolder(Args &&...args) : opt(std::forward<Args &&...>(args...)) {}
		template<typename ...Args>
		PtrHolder(Args... args) : opt(args...) {}
		PtrHolder(PtrHolder &&other) : opt(std::exchange(other.opt, std::nullopt)) {}
		PtrHolder(T *t) : opt(t) {}
		PtrHolder() : opt() {}

		template<typename V>
		PtrHolder &operator =(V *ptr)
		{
			opt = ptr;
			return *this;
		}
		template<typename V>
		PtrHolder &operator =(PtrHolder<V> &&other)
		{
			opt = std::move(std::exchange(other.opt, std::nullopt));
			return *this;
		}

		template<typename V>
		PtrHolder &operator =(const PtrHolder<V> &other)
		{
			opt = other.opt;
			return *this;
		}

		operator T*() { return opt.value_or(nullptr); }
		operator T*() const { return opt.value_or(nullptr); }
		operator const T*() const { return opt.value_or(nullptr); }

		T *value()
		{
			return opt.value();
		}
		T* value() const
		{
			return opt.value();
		}

		bool has_value() const
		{
			return opt.has_value();
		}

	private:
		std::optional<T *> opt;
	};

	// Note that JSContext actually changes behind the scenes when creating another ScriptRequest for another realm,
	// so be _very_ careful when juggling between different realms.
	PtrHolder<JSContext> cx;
	JS::HandleObject glob;
	JS::HandleObject nativeScope;
private:
	const ScriptInterface& m_ScriptInterface;
	PtrHolder<JS::Realm> m_FormerRealm;
};


#endif // INCLUDED_SCRIPTREQUEST
