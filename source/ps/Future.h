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

#ifndef INCLUDED_FUTURE
#define INCLUDED_FUTURE

#include "ps/execution/StopToken.h"

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

namespace FutureSharedStateDetail
{
struct VoidSubstitution {};
template<typename ResultType>
using NonVoid = std::conditional_t<std::is_void_v<ResultType>, VoidSubstitution, ResultType>;

/**
 * Responsible for syncronization between the task and the receiving thread.
 */
template<typename ResultType>
class Receiver
{
public:
	Receiver() = default;
	~Receiver()
	{
		ENSURE(IsDone());
	}

	Receiver(const Receiver&) = delete;
	Receiver(Receiver&&) = delete;

	void SetValue() noexcept requires std::same_as<ResultType, void>
	{
		m_Outcome.template emplace<FutureSharedStateDetail::VoidSubstitution>();
		SyncronizeOutcome();
	}

	void SetValue(NonVoid<ResultType>&& result) requires (!std::same_as<ResultType, void>)
	{
		m_Outcome.template emplace<ResultType>(std::forward<ResultType>(result));
		SyncronizeOutcome();
	}

	void SetError(std::exception_ptr ptr) noexcept
	{
		m_Outcome.template emplace<std::exception_ptr>(std::move(ptr));
		SyncronizeOutcome();
	}

	void SetStoped() noexcept
	{
		SyncronizeOutcome();
	}

	bool IsDone() const noexcept
	{
		return m_Done.load();
	}

	void Wait()
	{
		// Fast path: we're already done.
		if (IsDone())
			return;
		// Slow path: we aren't done when we run the above check. Lock and wait until we are.
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_ConditionVariable.wait(lock, [this]{ return IsDone(); });
	}

	/**
	 * Request the executing thread to stop as fast as possible. This is only
	 * a request the execution therad might ignore it.
	 * @see GetResult must not be called after a call to @p RequestStop.
	 */
	void RequestStop() noexcept
	{
		m_StopRequest.store(true);
	}

	StopToken GetStopToken() noexcept
	{
		return StopToken{m_StopRequest};
	}

	/**
	 * Move the result away from the shared state, mark the future invalid.
	 */
	ResultType GetResult()
	{
		// The caller must ensure that this is only called if there is a result.
		ENSURE(IsDone());
		ENSURE(std::holds_alternative<NonVoid<ResultType>>(m_Outcome) ||
			std::get<std::exception_ptr>(m_Outcome));

		if (std::holds_alternative<std::exception_ptr>(m_Outcome))
			std::rethrow_exception(std::exchange(std::get<std::exception_ptr>(m_Outcome), {}));

		[[maybe_unused]] auto ret = std::move(std::get<NonVoid<ResultType>>(m_Outcome));
		m_Outcome.template emplace<std::exception_ptr>();

		if constexpr (std::is_void_v<ResultType>)
			return;
		else
			return ret;
	}

private:
	void SyncronizeOutcome() noexcept
	{
		// Because we might have threads waiting on us, we need to make sure that they either:
		// - don't wait on our condition variable
		// - receive the notification when we're done.
		// This requires locking the mutex (@see Wait).
		{
			const std::lock_guard lock(m_Mutex);
			m_Done.store(true);
		}

		m_ConditionVariable.notify_all();
	}

	// This is only set by the executing thread and read by the receiving thread. It is never reset.
	std::atomic<bool> m_Done{false};
	// This is only set by the receiving thread and read by the executing thread. It is never reset.
	std::atomic<bool> m_StopRequest{false};
	std::mutex m_Mutex;
	std::condition_variable m_ConditionVariable;

	std::variant<std::exception_ptr, NonVoid<ResultType>> m_Outcome;
};
} // namespace FutureSharedStateDetail

template<typename State, typename Task, typename... PushArgs>
concept SharedState = std::constructible_from<State, Task> &&
	requires(State state, PushArgs&&... pushArgs)
{
	State::Push(state, std::forward<PushArgs>(pushArgs)...);
	{ &state.GetReceiver() } ->
		std::same_as<FutureSharedStateDetail::Receiver<typename Task::ResultType>*>;
};

template<typename T, typename... PushArgs>
concept ExecutionTask =
	(std::move_constructible<typename T::ResultType> || std::same_as<typename T::ResultType, void>) &&
	SharedState<typename T::template SharedState<FutureSharedStateDetail::Receiver>, T, PushArgs...>;

/**
 * Corresponds to std::future.
 * Unlike std::future, Future can request the cancellation of the task that would produce the result.
 * This makes it more similar to Java's CancellableTask or C#'s Task.
 * The name Future was kept over Task so it would be more familiar to C++ users,
 * but this all should be revised once Concurrency TS wraps up.
 *
 * Future is _not_ thread-safe. Call it from a single thread or ensure synchronization externally.
 *
 * The callback never runs after the @p Future is destroyed.
 */
template<typename ResultType>
class Future
{
	template<typename T>
	friend class PackagedTask;

public:
	Future() = default;
	Future(const Future& o) = delete;
	Future(Future&&) = default;
	Future& operator=(Future&& other)
	{
		CancelOrWait();
		m_Receiver = std::move(other.m_Receiver);
		return *this;
	}

	/**
	 * Make the future wait for the result of @a task.
	 */
	template<typename Scheduler, typename... Args, ExecutionTask<Scheduler&, Args...> Task>
	Future(Scheduler& taskManager, Task&& task, Args&&... args)
	{
		using TaskResultType = typename Task::ResultType;
		using TaskSharedState = typename Task::template SharedState<FutureSharedStateDetail::Receiver>;
		static_assert(std::is_same_v<TaskResultType, ResultType>,
		"The return type of the wrapped function is not the same as the type the Future expects.");

		auto temp = std::make_shared<TaskSharedState>(std::forward<Task>(task));

		TaskSharedState::Push(*temp, taskManager, std::forward<Args>(args)...);
		const auto receiverPtr = &temp->GetReceiver();
		m_Receiver = {std::move(temp), receiverPtr};
	}

	~Future()
	{
		CancelOrWait();
	}

	/**
	 * Move the result out of the future, and invalidate the future.
	 * If the future is not complete, calls Wait().
	 * If the future is invalid, asserts.
	 */
	ResultType Get()
	{
		ENSURE(!!m_Receiver);

		Wait();
		// This mark the state invalid - can't call Get again.
		return std::exchange(m_Receiver, nullptr)->GetResult();
	}

	/**
	 * @return true if the shared state is valid and has a result (i.e. Get can be called).
	 */
	bool IsDone() const
	{
		return !!m_Receiver && m_Receiver->IsDone();
	}

	/**
	 * @return true if the future has a shared state and it's not been invalidated, ie. pending, started or done.
	 */
	bool Valid() const
	{
		return !!m_Receiver;
	}

	void Wait()
	{
		if (Valid())
			m_Receiver->Wait();
	}

	void CancelOrWait()
	{
		if (!Valid())
			return;
		m_Receiver->RequestStop();
		m_Receiver->Wait();
		m_Receiver.reset();
	}

protected:
	std::shared_ptr<FutureSharedStateDetail::Receiver<ResultType>> m_Receiver;
};

template<typename Scheduler, typename... Args, ExecutionTask<Scheduler&, Args...> Task>
Future(Scheduler& taskManager, Task&& callback, Args&&... args) -> Future<typename Task::ResultType>;

#endif // INCLUDED_FUTURE
