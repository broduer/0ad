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

#ifndef INCLUDED_SCRIPTTHREAD
#define INCLUDED_SCRIPTTHREAD

#include "ps/Singleton.h"
#include "ps/Future.h"

#include "scriptinterface/ScriptContext.h"

#include <functional>
#include <utility>

#include "ps/Thread.h"

class ScriptThread : public Singleton<ScriptThread>
{
public:
	using IdType = u32;

	ScriptThread();
	~ScriptThread();
	ScriptThread(const ScriptThread&) = delete;
	ScriptThread(ScriptThread&&) = delete;
	ScriptThread& operator=(const ScriptThread&) = delete;
	ScriptThread& operator=(ScriptThread&&) = delete;

	// Accept any callable so lambdas (with unique types) can be passed directly
	// without requiring an implicit conversion to std::function which doesn't
	// participate in template argument deduction.
	template<typename Callback>
	inline auto Run(Callback&& func)
	{
		return Future<std::invoke_result_t<Callback>, ScriptThread>(*this, std::forward<Callback>(func));
	}

	template<typename Callback, typename... Args>
	inline auto Run(Callback&& func, Args&&... args)
	{
		return Future<std::invoke_result_t<Callback, Args...>, ScriptThread>(*this, std::forward<Callback>(func), std::forward<Args>(args)...);
	}

	template<typename Callback, typename... Args>
	inline auto Run(const Callback& func, Args&&... args)
	{
		return Future<std::invoke_result_t<Callback, Args...>, ScriptThread>(*this, std::forward<Callback>(func), std::forward<Args>(args)...);
	}

	template<typename Callback, typename... Args>
	inline auto Run(Callback&& func, Args&... args)
	{
		return Future<std::invoke_result_t<Callback, Args&...>, ScriptThread>(*this, std::forward<Callback>(func), args...);
	}

	template<typename Callback, typename... Args>
	inline auto Run(const Callback& func, const Args&... args)
	{
		return Future<std::invoke_result_t<Callback, const Args...>, ScriptThread>(*this, std::forward<Callback>(func), args...);
	}

	template<typename Callback, typename... Args>
	inline auto Run(Callback&& func, Args const&... args)
	{
		return Future<std::invoke_result_t<Callback, const Args...>, ScriptThread>(*this, std::forward<Callback>(func), args...);
	}

	std::shared_ptr<ScriptContext> Context;

	static std::thread::id GetCurrentThreadId();
	static bool IsThreadWorking();

protected:
	IdType PushTask(std::function<void()> &&func);
	static void RemoveTask(const IdType id);

private:
	class Impl;
	friend class Threading::WorkerThread<Impl>;
	template<typename T, typename M>
	friend class Future;

	const std::unique_ptr<Impl> m;
};

#define g_ScriptThread ScriptThread::GetSingleton()

template<typename Callback, typename... Args>
inline auto ScriptCommand(Callback&& func, Args&&... args)
{
	return g_ScriptThread.Run(std::forward<Callback>(func), std::forward<Args>(args)...);
}

#endif // INCLUDED_SCRIPTTHREAD
