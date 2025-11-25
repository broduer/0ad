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

#ifndef INCLUDED_SINGLE
#define INCLUDED_SINGLE

#include "ps/execution/StopToken.h"

namespace PS
{
/**
 * This task type denotes a callback to be executed once. When the task is
 * canceled it's not guaranteed that the callabck is executed.
 */
template<TokenInvocable Callback>
struct Single
{
	using ResultType = typename std::conditional_t<std::is_invocable_v<Callback, StopToken>,
		std::invoke_result<Callback, StopToken>, std::invoke_result<Callback>>::type;

	template<template<typename> typename Receiver>
	class SharedState;

	Callback callback;
};

template<TokenInvocable Callback>
Single(Callback) -> Single<Callback>;

/**
 * The shared state between futures and packaged state.
 */
template<TokenInvocable Callback>
template<template<typename> typename Receiver>
class Single<Callback>::SharedState
{
public:
	template<typename Task>
	SharedState(Task&& task) :
		callback{std::forward<Task>(task).callback}
	{}

	Receiver<ResultType>& GetReceiver() noexcept
	{
		return receiver;
	}

	template<typename... Args>
	static void Push(SharedState& task, auto& taskManager, Args&&... args)
	{
		taskManager.PushTask(PackagedTask{task}, std::forward<Args>(args)...);
	}

private:
	class PackagedTask;

	Callback callback;
	Receiver<ResultType> receiver;
};

/**
 * Corresponds somewhat to std::packaged_task.
 * Like packaged_task, this holds a function acting as a promise.
 * This type is mostly just the shared state and the call operator,
 * handling the promise & continuation logic.
 */
template<TokenInvocable Callback>
template<template<typename> typename Receiver>
class Single<Callback>::SharedState<Receiver>::PackagedTask
{
public:
	PackagedTask() = delete;
	PackagedTask(SharedState& ss) :
		m_SharedState(ss)
	{}

	void operator()()
	{
		if (m_SharedState.receiver.GetStopToken().IsStopRequested())
		{
			m_SharedState.receiver.SetStoped();
			return;
		}

		try
		{
			if constexpr (std::is_void_v<ResultType>)
			{
				Invoke();
				m_SharedState.receiver.SetValue();
			}
			else
				m_SharedState.receiver.SetValue(Invoke());
		}
		catch(...)
		{
			m_SharedState.receiver.SetError(std::current_exception());
		}
	}

private:
	ResultType Invoke()
	{
		if constexpr (std::is_invocable_v<Callback, StopToken>)
			return m_SharedState.callback(m_SharedState.receiver.GetStopToken());
		else
			return m_SharedState.callback();
	}

	SharedState& m_SharedState;
};
}

#endif // INCLUDED_SINGLE
