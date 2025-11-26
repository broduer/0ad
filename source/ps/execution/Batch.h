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

#ifndef INCLUDED_BATCH
#define INCLUDED_BATCH

#include "ps/execution/StopToken.h"

#include <concepts>
#include <ranges>

namespace PS
{
/**
 * This task type denotes a callback to be executed for each element in the
 * range. If the callback returns a result, the result is ignored. When the
 * callback throws an exception or the task is canceled, it's not guaranteed
 * that the callback is invoked with every element of the range.
 */
template<std::ranges::input_range Range, TokenInvocable<std::ranges::range_reference_t<Range>> Callback>
	requires std::ranges::sized_range<Range>
struct Batch
{
	using ResultType = void;

	template<template<typename> typename Receiver>
	class SharedState;

	Range range;
	Callback callback;
};

// Use `all_t` to handle r- and l- values.
template<std::ranges::input_range Range, TokenInvocable<std::ranges::range_reference_t<Range>> Callback>
	requires std::ranges::sized_range<Range>
Batch(Range&&, Callback) -> Batch<std::views::all_t<Range>, Callback>;

/**
 * The shared state between futures and packaged state.
 */
template<std::ranges::input_range Range, TokenInvocable<std::ranges::range_reference_t<Range>> Callback>
	requires std::ranges::sized_range<Range>
template<template<typename> typename Receiver>
class Batch<Range, Callback>::SharedState
{
public:
	using ElementType = std::ranges::range_reference_t<Range>;

	SharedState(Batch task) :
		range{std::move(task.range)},
		callback{std::move(task.callback)}
	{
		ENSURE(std::size(range) < std::numeric_limits<std::int8_t>::max());
	}

	Receiver<ResultType>& GetReceiver() noexcept
	{
		return receiver;
	}

	template<typename... Args>
	static void Push(SharedState& task, auto& taskManager, Args&&... args)
	{
		for (auto&& element : task.range)
			taskManager.PushTask(PackagedTask{task, std::forward<ElementType>(element)}, args...);

		if (task.range.empty())
			task.receiver.SetValue();
	}

private:
	Range range;
	Callback callback;
	std::atomic<std::int8_t> completionCount{static_cast<std::int8_t>(std::size(range))};
	Receiver<ResultType> receiver;

	void SetException(std::exception_ptr ptr)
	{
		const std::lock_guard lock{mutex};
		exception = std::move(ptr);
	}

	bool HasException()
	{
		const std::lock_guard lock{mutex};
		return !!exception;
	}

	std::mutex mutex;
	std::exception_ptr exception;

	class PackagedTask;
};

template<std::ranges::input_range Range, TokenInvocable<std::ranges::range_reference_t<Range>> Callback>
	requires std::ranges::sized_range<Range>
template<template<typename> typename Receiver>
class Batch<Range, Callback>::SharedState<Receiver>::PackagedTask
{
public:
	PackagedTask() = delete;
	PackagedTask(SharedState& state, ElementType&& element) :
		m_SharedState{state},
		m_Element{std::forward<ElementType>(element)}
	{}

	void operator()()
	{
		if (!m_SharedState.receiver.GetStopToken().IsStopRequested() && !m_SharedState.HasException())
		{
			try
			{
				Invoke();
			}
			catch(...)
			{
				std::lock_guard lock{m_SharedState.mutex};
				m_SharedState.exception = std::current_exception();
			}
		}

		if (m_SharedState.completionCount.fetch_sub(1) != 1)
			return;

		if (m_SharedState.receiver.GetStopToken().IsStopRequested())
		{
			m_SharedState.receiver.SetStoped();
			return;
		}
		// Mutex isn't required because all other `PackagedTask`s are already finished.
		if (m_SharedState.exception)
		{
			m_SharedState.receiver.SetError(std::move(m_SharedState.exception));
			return;
		}
		m_SharedState.receiver.SetValue();
	}

private:
	ResultType Invoke()
	{
		if constexpr (std::is_invocable_v<Callback, StopToken>)
		{
			return m_SharedState.callback(m_SharedState.receiver.GetStopToken(),
				std::forward<ElementType>(m_Element));
		}
		else
			return m_SharedState.callback(std::forward<ElementType>(m_Element));
	}

	SharedState& m_SharedState;
	ElementType m_Element;
};
}

#endif // INCLUDED_BATCH
