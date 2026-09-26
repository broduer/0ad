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
#include "ps/ThreadAffinity.h"
#include "ps/Threading.h"

#include <array>
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
		m_Thread = Threading::CreateThread(HandleExceptions<DoStart<T, callable>>::Wrapper, object);
	}
	template<typename T, void(T::* callable)()>
	static void DoStart(T* object);

protected:
	~Thread()
	{
		ENSURE(!m_Thread.joinable());
	}

	std::thread m_Thread;
	std::atomic<bool> m_Kill = false;
};

/**
 * Worker thread: process the taskManager queues until killed.
 */
class WorkerThread : public Thread
{
public:
	WorkerThread(TaskManager::Impl& taskManager);
	~WorkerThread();

	/**
	 * Wake the worker.
	 */
	void Wake();

protected:
	void RunUntilDeath();

	std::mutex m_Mutex;
	std::condition_variable m_ConditionVariable;

	TaskManager::Impl& m_TaskManager;
};

/**
 * PImpl-ed implementation of the Task manager.
 *
 * The queues are processed in order of priority, each only if there are no
 * higher-priority tasks. Normal and low priority tasks leave some workers free,
 * so high-priority tasks start at once however many others are running.
 */
class TaskManager::Impl
{
	friend class TaskManager;
	friend class WorkerThread;
public:
	Impl() = default;
	~Impl()
	{
		for (Queue& queue : m_Queues)
		{
			std::lock_guard<std::mutex> lock(queue.mutex);
			ENSURE(queue.items.empty());
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
	void PushTask(std::function<void()>&& task, TaskPriority priority);

protected:
	void ClearQueue();

	bool PopTask(TaskPriority priority, std::function<void()>& taskOut);

	/**
	 * Return whether a worker has a task it may take.
	 */
	bool HasWorkForWorker() const;

	struct Queue
	{
		std::atomic<bool> hasWork = false;
		std::mutex mutex;
		std::deque<QueueItem> items;
	};
	std::array<Queue, 3> m_Queues;

	// Workers running normal or low priority tasks, at most m_LowerPriorityLimit.
	std::atomic<size_t> m_LowerPriorityRunning = 0;
	size_t m_LowerPriorityLimit = 0;

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
	m_LowerPriorityLimit = numberOfWorkers - numberOfWorkers / 4;
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
	Queue& queue = m_Queues[static_cast<size_t>(priority)];
	{
		std::lock_guard<std::mutex> lock(queue.mutex);
		queue.items.emplace_back(std::move(task));
		queue.hasWork = true;
	}

	for (WorkerThread& worker : m_Workers)
		worker.Wake();
}

bool TaskManager::Impl::PopTask(TaskPriority priority, std::function<void()>& taskOut)
{
	Queue& queue = m_Queues[static_cast<size_t>(priority)];

	// Particularly critical section since we're locking the global queue.
	std::lock_guard<std::mutex> globalLock(queue.mutex);
	if (!queue.items.empty())
	{
		taskOut = std::move(queue.items.front());
		queue.items.pop_front();
		queue.hasWork = !queue.items.empty();
		return true;
	}
	return false;
}

bool TaskManager::Impl::HasWorkForWorker() const
{
	if (m_Queues[static_cast<size_t>(TaskPriority::HIGH)].hasWork)
		return true;
	return m_LowerPriorityRunning < m_LowerPriorityLimit &&
		(m_Queues[static_cast<size_t>(TaskPriority::NORMAL)].hasWork ||
		m_Queues[static_cast<size_t>(TaskPriority::LOW)].hasWork);
}

// Thread definition

WorkerThread::WorkerThread(TaskManager::Impl& taskManager)
	: m_TaskManager(taskManager)
{
	Start<WorkerThread, &WorkerThread::RunUntilDeath>(this);
}

WorkerThread::~WorkerThread()
{
	m_Kill = true;
	m_ConditionVariable.notify_one();
	if (m_Thread.joinable())
		m_Thread.join();
}

void WorkerThread::Wake()
{
	m_ConditionVariable.notify_one();
}

void WorkerThread::RunUntilDeath()
{
	// The profiler does better if the names are unique.
	static std::atomic<int> n = 0;
	std::string name = "Task Mgr #" + std::to_string(n++);
	debug_SetThreadName(name.c_str());
	g_Profiler2.RegisterCurrentThread(name);
	ThreadAffinity::KeepCurrentThreadOffRenderCore();


	std::function<void()> task;
	bool hasTask = false;
	std::unique_lock<std::mutex> lock(m_Mutex, std::defer_lock);
	while (!m_Kill)
	{
		lock.lock();
		m_ConditionVariable.wait(lock, [this](){
			return m_Kill || m_TaskManager.HasWorkForWorker();
		});
		lock.unlock();

		if (m_Kill)
			break;

		// Fetch work from the global queues.
		if (m_TaskManager.PopTask(TaskPriority::HIGH, task))
		{
			task();
			continue;
		}
		if (m_TaskManager.m_LowerPriorityRunning.fetch_add(1) < m_TaskManager.m_LowerPriorityLimit)
		{
			hasTask = m_TaskManager.PopTask(TaskPriority::NORMAL, task);
			if (!hasTask)
				hasTask = m_TaskManager.PopTask(TaskPriority::LOW, task);
			if (hasTask)
				task();
		}
		--m_TaskManager.m_LowerPriorityRunning;
	}
}

// Defined here - needs access to derived types.
template<typename T, void(T::* callable)()>
void Thread::DoStart(T* object)
{
	std::invoke(callable, object);
}

} // namespace Threading
