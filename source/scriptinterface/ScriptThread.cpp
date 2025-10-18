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

namespace
{
thread_local std::shared_ptr<ScriptContext> g_ScriptContext;
}

using QueueItem = std::function<void()>;

class ScriptThread::Impl
{
	friend class ScriptThread;
	friend class Threading::WorkerThread<ScriptThread::Impl>;
public:
	static constexpr auto TaskNamePrefix = "JS RT";

	Impl()
		: m_Worker(*this)
	{}

	~Impl()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		ENSURE(m_Queue.empty());
	}

	/**
	 * Push a task on the global queue.
	 * Takes ownership of @a task.
	 * May be called from any thread.
	 */
	void PushTask(std::function<void()>&& task) {
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Queue.emplace_back(std::move(task));
			m_HasWork = true;
		}

		m_Worker.Wake();
	}

protected:
	bool PopTask(std::function<void()>& taskOut)
	{
		// Particularly critical section since we're locking the global queue.
		std::lock_guard<std::mutex> globalLock(m_Mutex);
		if (!m_Queue.empty())
		{
			taskOut = std::move(m_Queue.front());
			m_Queue.pop_front();
			m_HasWork = !m_Queue.empty();
			return true;
		}

		return false;
	}

	std::atomic<bool> m_HasWork = false;
	std::mutex m_Mutex;
	std::deque<QueueItem> m_Queue;
	Threading::WorkerThread<ScriptThread::Impl> m_Worker;
};

ScriptThread::ScriptThread()
	: m{std::make_unique<Impl>()}
{
}

ScriptThread::~ScriptThread() = default;

void ScriptThread::PushTask(std::function<void()> task)
{
	m->PushTask(std::move(task));
}
