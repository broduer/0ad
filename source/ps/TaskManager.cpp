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

#include "precompiled.h"

#include "TaskManager.h"

#include "lib/debug.h"
#include "maths/MathUtil.h"
#include "ps/Profiler2.h"
#include "ps/Threading.h"

#include <atomic>
#include <boost/lockfree/queue.hpp>
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
 * Light wrapper around std::thread. Ensures Join has been called.
 */
class Thread
{
public:
	Thread() = default;
	Thread(const Thread&) = delete;
	Thread(Thread&&) = delete;

	template<typename T, void(T::* callable)()>
	void Start(T* object)
	{
		m_Thread = std::thread(HandleExceptions<DoStart<T, callable>>::Wrapper, object);
	}
	template<typename T, void(T::* callable)()>
	static void DoStart(T* object);

protected:
	~Thread()
	{
		ENSURE(!m_Thread.joinable());
	}

	std::thread m_Thread;
};

/**
 * Worker thread: process the taskManager queues until killed.
 */
class WorkerThread : public Thread
{
public:
	WorkerThread(TaskManager::Impl& taskManager);
	~WorkerThread();

protected:
	void RunUntilDeath();
	TaskManager::Impl& m_TaskManager;
};

/**
 * PImpl-ed implementation of the Task manager.
 *
 * The normal priority queue is processed first, the low priority only if there are no higher-priority tasks
 */
class TaskManager::Impl
{
	friend class TaskManager;
	friend class WorkerThread;
public:
	Impl() = default;
	~Impl()
	{
			ENSURE(m_Tasks == 0);
			ENSURE(m_GlobalQueue.empty());
			ENSURE(m_GlobalLowPriorityQueue.empty());
			m_Tasks = SHUTDOWN_VALUE;
			m_CV.notify_all();
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
	void PushTask(std::function<void()>&& task, TaskPriority priority);

protected:
	template<TaskPriority Priority>
	bool PopTask(std::function<void()>& taskOut);

	static constexpr int SHUTDOWN_VALUE = -1;
	std::atomic<int> m_Tasks = 0;
	// We can't use C++20 atomic wait/notify so we need a condition variable/mutex pair
	std::condition_variable m_CV;
	std::mutex m_Mutex;
	// boost::lockfree::queue only support trivial types so we have to store naked owning pointers
	boost::lockfree::queue<QueueItem*> m_GlobalQueue{MAX_WORKERS};
	boost::lockfree::queue<QueueItem*> m_GlobalLowPriorityQueue{MAX_WORKERS};

	// Ideally this would be a vector, since it does get iterated, but that requires movable types.
	std::deque<WorkerThread> m_Workers;
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

void TaskManager::Impl::SetupWorkers(size_t numberOfWorkers)
{
	for (size_t i = 0; i < numberOfWorkers; ++i)
		m_Workers.emplace_back(*this);
}

size_t TaskManager::GetNumberOfWorkers() const
{
	return m->m_Workers.size();
}

void TaskManager::PushTask(std::function<void()> task, TaskPriority priority)
{
	m->PushTask(std::move(task), priority);
}

void TaskManager::Impl::PushTask(std::function<void()>&& task, TaskPriority priority)
{
	auto& queue = priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;
	queue.push(new QueueItem(std::move(task)));
	++m_Tasks;
	m_CV.notify_one();
}

template<TaskPriority Priority>
bool TaskManager::Impl::PopTask(std::function<void()>& taskOut)
{
	auto& queue = Priority == TaskPriority::NORMAL ? m_GlobalQueue : m_GlobalLowPriorityQueue;

	QueueItem* item;
	if (queue.pop(item))
	{
		--m_Tasks;
		taskOut = std::move(*item);
		delete item;
		return true;
	}
	return false;
}

// Thread definition

WorkerThread::WorkerThread(TaskManager::Impl& taskManager)
	: m_TaskManager(taskManager)
{
	Start<WorkerThread, &WorkerThread::RunUntilDeath>(this);
}

WorkerThread::~WorkerThread()
{
	ENSURE(m_TaskManager.m_Tasks == TaskManager::Impl::SHUTDOWN_VALUE);
	m_Thread.join();
}

void WorkerThread::RunUntilDeath()
{
	// The profiler does better if the names are unique.
	static std::atomic<int> n = 0;
	std::string name = "Task Mgr #" + std::to_string(n++);
	debug_SetThreadName(name.c_str());
	g_Profiler2.RegisterCurrentThread(name);

	std::function<void()> task;
	bool hasTask = false;
	int availableTasks = m_TaskManager.m_Tasks.load();
	while (availableTasks != TaskManager::Impl::SHUTDOWN_VALUE)
	{
		if (availableTasks > 0)
		{
			// Fetch work from the global queues.
			hasTask = m_TaskManager.PopTask<TaskPriority::NORMAL>(task);
			if (!hasTask)
				hasTask = m_TaskManager.PopTask<TaskPriority::LOW>(task);
			if (hasTask)
				task();
		}
		else
		{
			std::unique_lock lock(m_TaskManager.m_Mutex);
			m_TaskManager.m_CV.wait(lock);
		}
		availableTasks = m_TaskManager.m_Tasks.load();
	}
}

// Defined here - needs access to derived types.
template<typename T, void(T::* callable)()>
void Thread::DoStart(T* object)
{
	std::invoke(callable, object);
}

} // namespace Threading
