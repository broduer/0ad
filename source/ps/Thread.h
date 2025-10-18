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

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Threading
{

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
    // Defined here - needs access to derived types.
    template<typename T, void(T::* callable)()>
    static void DoStart(T* object)
    {
        std::invoke(callable, object);
    }

protected:
    ~Thread()
    {
        ENSURE(!m_Thread.joinable());
    }

    std::thread m_Thread;
    std::atomic<bool> m_Kill = false;
};

namespace details
{

template <size_t N>
struct StringLiteral {
	constexpr StringLiteral(const char (&str)[N]) {
		std::copy_n(str, N, value);
	}
	char value[N];
};

}

/**
 * Worker thread: process the taskManager queues until killed.
 */
template<class Manager>
class WorkerThread : public Thread
{
public:
    explicit WorkerThread(Manager& taskManager)
    : m_TaskManager(taskManager)
    {
        Start<WorkerThread, &WorkerThread::RunUntilDeath>(this);
    }

    ~WorkerThread()
    {
        m_Kill = true;
        m_ConditionVariable.notify_one();
        if (m_Thread.joinable())
            m_Thread.join();
    }

    /**
     * Wake the worker.
     */
    void Wake()
    {
        m_ConditionVariable.notify_one();
    }

protected:
    void RunUntilDeath()
    {
        // The profiler does better if the names are unique.
        static std::atomic<int> n = 0;
        const std::string name = Manager::TaskNamePrefix + std::to_string(n++);
        debug_SetThreadName(name.c_str());
        g_Profiler2.RegisterCurrentThread(name);

        bool hasTask = false;
        std::unique_lock<std::mutex> lock(m_Mutex, std::defer_lock);
        while (!m_Kill)
        {
            lock.lock();
            m_ConditionVariable.wait(lock, [this](){
                return m_Kill || m_TaskManager.m_HasWork || m_TaskManager.m_HasLowPriorityWork;
            });
            lock.unlock();

            if (m_Kill)
            {
				break;
            }

        	std::function<void()> task;
            // Fetch work from the global queues.
            hasTask = m_TaskManager.template PopTask<TaskPriority::NORMAL>(task);
            if (!hasTask)
                hasTask = m_TaskManager.template PopTask<TaskPriority::LOW>(task);
            if (hasTask)
                task();
        }
    }

    std::mutex m_Mutex;
    std::condition_variable m_ConditionVariable;

    Manager& m_TaskManager;
};

}

#endif // INCLUDED_THREAD
