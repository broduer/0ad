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

#ifndef INCLUDED_THREAD
#define INCLUDED_THREAD

#include "ps/Threading.h"
#include "ps/Profiler2.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <algorithm>
#include <sstream>

namespace Threading
{

/**
 * Light wrapper around std::thread. Ensures Join has been called.
 */
template<class Manager, class Specifics>
class Thread
{
public:
    explicit Thread(Manager &taskManager)
		: m_TaskManager(taskManager)
    {
        Start<Thread, &Thread::RunUntilDeath>(this);
    }

    Thread(const Thread&) = delete;
    Thread(Thread&&) = delete;

    template<typename T, void(T::* callable)()>
    void Start(T* object)
    {
        m_Thread = std::thread(HandleExceptions<DoStart<T, callable>>::Wrapper, object);
    }
    // Defined here - needs access to derived types.
    template<typename T, void(T::* callable)()>
    static void DoStart(T* object)
    {
        std::invoke(callable, object);
    }

	/**
	 * Wake the worker.
	 */
	void Wake()
    {
    	m_ConditionVariable.notify_one();
    }

	auto GetLocalId() const { return local_id; }
	auto GetThreadId() const { return thread_id; }

protected:
    ~Thread()
    {
    	m_Kill = true;
    	m_ConditionVariable.notify_one();
    	if (m_Thread.joinable())
    		m_Thread.join();
    }

	void RunUntilDeath()
    {
    	// The profiler does better if the names are unique.
    	static std::atomic n = 0;
    	local_id = n++;
    	thread_id = std::this_thread::get_id();
    	const std::string name = Manager::TaskNamePrefix + std::to_string(local_id);
    	debug_SetThreadName(name.c_str());
    	g_Profiler2.RegisterCurrentThread(name);

#ifdef DEBUG_THREAD_PULLS
    	std::ostringstream os;
    	os << thread_id;
    	debug_printf("Starting thread %s, TID: %s\n", name.c_str(), os.str().c_str());
#endif

    	std::unique_lock lock(m_Mutex, std::defer_lock);
    	while (!m_Kill)
    	{
    		lock.lock();
    		m_ConditionVariable.wait(lock, [this]
			{
				return Specifics::CheckCondition(*static_cast<Specifics *>(this));
			});
    		lock.unlock();

    		if (m_Kill)
    		{
    			break;
    		}

    		std::function<void()> task;
    		if (Specifics::PopTask(*static_cast<Specifics *>(this), task))
    		{
    			task();
    		}
    	}
    };

    std::thread m_Thread;
	std::atomic<bool> m_Kill = false;
	std::mutex m_Mutex;
	std::condition_variable m_ConditionVariable;
	int local_id = -1;
	std::thread::id thread_id;

	Manager& m_TaskManager;
};

/**
 * Worker thread: process the taskManager queues until killed.
 */
template<class Manager>
class WorkerThread : public Thread<Manager, WorkerThread<Manager>>
{
public:
    explicit WorkerThread(Manager& taskManager)
	    : Thread<Manager, WorkerThread>(taskManager)
    {
    }

protected:
	static bool CheckCondition(WorkerThread &t)
    {
    	return t.m_Kill || t.m_TaskManager.m_HasWork;
    }

	static bool PopTask(WorkerThread &t, std::function<void()> &task)
    {
    	return t.m_TaskManager.PopTask(task);
    }

	friend class Thread<Manager, WorkerThread>;
};

/**
 * Worker thread: process the taskManager queues until killed.
 */
template<class Manager>
class PrioritizedWorkerThread : public Thread<Manager, PrioritizedWorkerThread<Manager>>
{
public:
	explicit PrioritizedWorkerThread(Manager& taskManager)
	: Thread<Manager, PrioritizedWorkerThread>(taskManager)
	{
	}

protected:
	static bool CheckCondition(PrioritizedWorkerThread &t)
	{
		return t.m_Kill || t.m_TaskManager.m_HasWork || t.m_TaskManager.m_HasLowPriorityWork;
	}

	static bool PopTask(PrioritizedWorkerThread &t, std::function<void()> &task)
	{
		bool hasTask = t.m_TaskManager.template PopTask<TaskPriority::NORMAL>(task);
		if (!hasTask)
		{
			hasTask = t.m_TaskManager.template PopTask<TaskPriority::LOW>(task);
		}

		return hasTask;
	}

	friend class Thread<Manager, PrioritizedWorkerThread>;
};

}

#endif // INCLUDED_THREAD
