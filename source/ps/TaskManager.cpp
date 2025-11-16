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

#include "precompiled.h"

#include "TaskManager.h"

#include "lib/debug.h"
#include "maths/MathUtil.h"
#include "ps/Profiler2.h"
#include "ps/Thread.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Threading
{

namespace
{
/**
 * Minimum number of TaskManager workers.
 */
constexpr size_t MIN_WORKERS = 3;

/**
 * Maximum number of TaskManager workers.
 */
constexpr size_t MAX_WORKERS = 32;

size_t GetDefaultNumberOfWorkers()
{
	const size_t hardware_concurrency = std::thread::hardware_concurrency();
	return hardware_concurrency ? Clamp(hardware_concurrency - 1, MIN_WORKERS, MAX_WORKERS) : MIN_WORKERS;
}

} // anonymous namespace

using QueueItem = std::function<void()>;

/**
 * PImpl-ed implementation of the Task manager.
 *
 * The normal priority queue is processed first, the low priority only if there are no higher-priority tasks
 */
class TaskManager::Impl
{
	friend class TaskManager;
	friend class WorkerThread<TaskManager::Impl>;
public:
	static constexpr auto TaskNamePrefix = "Task Mgr #";

	Impl() = default;
	~Impl()
	{
		{
			std::lock_guard lock(m_Mutex);
			ENSURE(m_GlobalQueue.empty());
		}
		{
			std::lock_guard lock(m_GlobalLowPriorityMutex);
			ENSURE(m_GlobalLowPriorityQueue.empty());
		}
	}

	/**
	 * 2-phase init to avoid having to think too hard about the order of class members.
	 */
	void SetupWorkers(size_t numberOfWorkers);

	/**
	 * Push a task on the global queue.
	 * Takes ownership of @a task.
	 * May be called from any thread.
	 */
	IdType PushTask(std::function<void()>&& task, TaskPriority priority);

protected:
	void ClearQueue(); // [TODO] May as well be removed if unused.

	bool PopTask(std::function<void()>& taskOut);
	template<TaskPriority Priority>
	bool PopTask(std::function<void()>& taskOut);

	std::atomic<bool> m_HasWork = false;
	std::atomic<bool> m_HasLowPriorityWork = false;
	std::mutex m_Mutex;
	std::mutex m_GlobalLowPriorityMutex;
	std::deque<QueueItem> m_GlobalQueue;
	std::deque<QueueItem> m_GlobalLowPriorityQueue;

	// Ideally this would be a vector, since it does get iterated, but that requires movable types.
	std::deque<WorkerThread<TaskManager::Impl>> m_Workers;

	IdType m_LastIdentifier = 0;
	std::set<std::thread::id> m_WorkingThreads;
};

TaskManager::TaskManager() : TaskManager(GetDefaultNumberOfWorkers())
{
}

TaskManager::TaskManager(size_t numberOfWorkers)
	: m{std::make_unique<Impl>()}
{
	numberOfWorkers = Clamp<size_t>(numberOfWorkers, MIN_WORKERS, MAX_WORKERS);
	m->SetupWorkers(numberOfWorkers);
}

TaskManager::~TaskManager() = default;

void TaskManager::Impl::SetupWorkers(const size_t numberOfWorkers)
{
	for (size_t i = 0; i < numberOfWorkers; ++i)
		m_Workers.emplace_back(*this);
}

size_t TaskManager::GetNumberOfWorkers() const
{
	return m->m_Workers.size();
}

TaskManager::IdType TaskManager::PushTask(std::function<void()> task, const TaskPriority priority)
{
	return m->PushTask(std::move(task), priority);
}

bool TaskManager::IsThreadWorking()
{
	std::lock_guard globalLock(GetSingleton().m->m_Mutex);

	return GetSingleton().m->m_WorkingThreads.contains(std::this_thread::get_id());
}

TaskManager::IdType TaskManager::Impl::PushTask(std::function<void()>&& task, const TaskPriority priority)
{
	IdType id;

	std::deque<QueueItem>& queue = priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;
	{
		std::lock_guard lock(m_Mutex);
		id = m_LastIdentifier++;
		queue.emplace_back(std::move(task));

		(priority == TaskPriority::NORMAL ? m_HasWork : m_HasLowPriorityWork) = true;
	}

	for (auto& worker : m_Workers)
		worker.Wake();

	return id;
}

bool TaskManager::Impl::PopTask(std::function<void()>& taskOut)
{
	if (!PopTask<TaskPriority::NORMAL>(taskOut))
		return PopTask<TaskPriority::LOW>(taskOut);

	return true;
}

template<TaskPriority Priority>
bool TaskManager::Impl::PopTask(std::function<void()>& taskOut)
{
	// Particularly critical section since we're locking the global queue.
	std::lock_guard globalLock(m_Mutex);

	std::deque<QueueItem>& queue = Priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;
	std::atomic<bool>& hasWork = Priority == TaskPriority::NORMAL ? m_HasWork : m_HasLowPriorityWork;

	if (!queue.empty())
	{
		m_WorkingThreads.insert(std::this_thread::get_id());
		taskOut = std::move(queue.front());
		queue.pop_front();
		hasWork = !queue.empty();
		return true;
	}

	m_WorkingThreads.erase(std::this_thread::get_id());

	return false;
}

} // namespace Threading
