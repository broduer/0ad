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

#include "ScriptThread.h"

#include "scriptinterface/ScriptContext.h"

#include <atomic>
#include <thread>
#include <deque>
#include <sstream>
#include <mutex>

namespace
{
extern thread_local std::shared_ptr<ScriptContext> g_ScriptContext;
}

class ScriptThread::Impl
{
	friend class ScriptThread;
	friend class Threading::WorkerThread<ScriptThread::Impl>;
public:
	// Task ID, Source TID, Task
	using QueueItem = std::tuple<IdType, std::thread::id, std::function<void()>>;

	static constexpr auto TaskNamePrefix = "JS RT";

	Impl()
		: m_Worker(*this)
	{}

	~Impl()
	{
		std::lock_guard lock(m_Mutex);
		ENSURE(m_Queue.empty());
	}

	/**
	 * Push a task on the global queue.
	 * Takes ownership of @a task.
	 * May be called from any thread.
	 */
	IdType PushTask(std::function<void()>&& task) {
		IdType id;

		{
			std::lock_guard lock(m_Mutex);
			id = m_LastIdentifier++;
			m_Queue.emplace_back(id, std::this_thread::get_id(), std::move(task));
			m_HasWork = true;

			std::ostringstream os;
			os << "src: " << std::this_thread::get_id() << " target: " << m_Worker.GetThreadId() << " pushed: " << id << "\n";
			debug_printf(os.str().c_str());
		}

		m_Worker.Wake();
		return id;
	}

protected:
	bool PopTask(std::function<void()>& taskOut)
	{
		// Particularly critical section since we're locking the global queue.
		std::lock_guard globalLock(m_Mutex);
		if (!m_Queue.empty())
		{
			auto tup = std::move(m_Queue.front());
			taskOut = std::move(std::get<2>(tup));
			m_CurrentThreadId = std::this_thread::get_id();
			m_Queue.pop_front();
			m_HasWork = !m_Queue.empty();

			std::ostringstream os;
			os << std::this_thread::get_id() << " pops " << std::get<0>(tup) << " pushed by " << m_CurrentThreadId << "\n";
			debug_printf(os.str().c_str());
			return true;
		} else {
			m_CurrentThreadId = std::thread::id();
		}

		return false;
	}

	void RemoveTask(const IdType id)
	{
		std::lock_guard globalLock(m_Mutex);
		// This function is theoretically only called when the currently running function waits for a task on the same thread.
		// Having it called when another thread is running would be a synchronization issue.
		ENSURE(m_CurrentThreadId == std::this_thread::get_id() && "Looks like a synchronization issue occurred, an invalid call occurred.");

		// We also do not need additional thread syncs primitives, as the caller is the only thread that can modify the queue at this point.

		std::ostringstream os;
		os << "src: " << std::this_thread::get_id() << " removes " << id << "\n";
		printf(os.str().c_str());
		const auto it = std::find_if(m_Queue.cbegin(), m_Queue.cend(),
			[id](const QueueItem& item) {
				return std::get<0>(item) == id;
			});
		if (it != m_Queue.end()) {
			std::get<2>(*it)();
			m_Queue.erase(it);
		}
	}

	IdType m_LastIdentifier = 0;
	std::atomic<bool> m_HasWork = false;
	std::recursive_mutex m_Mutex;
	std::deque<QueueItem> m_Queue;
	Threading::WorkerThread<Impl> m_Worker;
	std::atomic<std::thread::id> m_CurrentThreadId;
};

ScriptThread::ScriptThread()
	: m{std::make_unique<Impl>()}
{
}

ScriptThread::~ScriptThread() = default;

ScriptThread::IdType ScriptThread::PushTask(std::function<void()> &&task)
{
	return m->PushTask(std::move(task));
}

void ScriptThread::RemoveTask(const IdType id)
{
	GetSingletonPtr()->m->RemoveTask(id);
}

std::thread::id ScriptThread::GetCurrentThreadId()
{
	return GetSingletonPtr()->m->m_CurrentThreadId.load();
}

bool ScriptThread::IsThreadWorking()
{
	std::lock_guard globalLock(GetSingletonPtr()->m->m_Mutex);
	const auto v = GetCurrentThreadId();
	const bool out = v == std::this_thread::get_id();

	std::ostringstream os;
	os << "ScriptThread - IsThreadWorking: from " << std::this_thread::get_id() << " against " << v << " results " << out << "\n";
	debug_printf(os.str().c_str());

	return out;
}
