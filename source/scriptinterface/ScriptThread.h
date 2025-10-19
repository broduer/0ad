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

	std::shared_ptr<ScriptContext> Context;

	static std::thread::id GetCurrentThreadId();

protected:
	void PushTask(std::function<void()> &&func);

private:
	class Impl;
	friend class Threading::WorkerThread<Impl>;
	template<typename T, typename M>
	friend class FutureBase;

	const std::unique_ptr<Impl> m;
};

#define g_ScriptThread ScriptThread::GetSingleton()

#endif // INCLUDED_SCRIPTTHREAD
