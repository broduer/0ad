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

#ifndef INCLUDED_THREADING_TASKMANAGER
#define INCLUDED_THREADING_TASKMANAGER

#include "ps/Singleton.h"
#include "ps/Threading.h"

#include <functional>
#include <memory>
#include <thread>

template<typename R, typename M>
class FutureBase;

namespace Threading
{

template<typename Manager>
class PrioritizedWorkerThread;

/**
 * The task manager creates all worker threads on initialisation,
 * and manages the task queues.
 * See implementation for additional comments.
 */
class TaskManager : public Singleton<TaskManager>
{
public:
	using IdType = u32;

	TaskManager();
	~TaskManager();
	TaskManager(const TaskManager&) = delete;
	TaskManager(TaskManager&&) = delete;
	TaskManager& operator=(const TaskManager&) = delete;
	TaskManager& operator=(TaskManager&&) = delete;

	/**
	 * @return the number of threaded workers.
	 */
	size_t GetNumberOfWorkers() const;

	/**
	 * Push a task to be executed.
	 */
	IdType PushTask(std::function<void()> func, TaskPriority priority = TaskPriority::NORMAL);

	inline static std::thread::id GetCurrentThreadId()
	{
		return std::thread::id();
	}

	static bool IsThreadWorking();

protected:
	// For now, the function does nothing.
	// [TODO] Implement the same logic as ScriptThread, but considering each thread separately.
	// It may require some refactoring of the worker thread & thread manager ifaces (ST & TM classes).
	inline static void RemoveTask(const IdType id) { (void)(id); }

private:
	explicit TaskManager(size_t numberOfWorkers);

	class Impl;
	friend class PrioritizedWorkerThread<Impl>;
	template<typename T, typename M>
	friend class ::FutureBase;

	const std::unique_ptr<Impl> m;
};
} // namespace Threading

#define g_TaskManager Threading::TaskManager::GetSingleton()

#endif // INCLUDED_THREADING_TASKMANAGER
