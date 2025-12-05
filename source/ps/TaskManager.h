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

#include "maths/MathUtil.h"
#include "ps/Singleton.h"

#include <thread>
#include <unifex/static_thread_pool.hpp>

namespace Threading
{
class TaskManager : public Singleton<TaskManager>
{
public:
	unsigned int GetNumberOfWorkers()
	{
		return m_NumThreads;
	}

	auto get_scheduler()
	{
		return m_Pool.get_scheduler();
	}

private:

	unsigned int m_NumThreads{Clamp<unsigned int>(std::thread::hardware_concurrency(), 4, 33) - 1};
	unifex::static_thread_pool m_Pool{m_NumThreads};
};
} // namespace Threading

#define g_TaskManager Threading::TaskManager::GetSingleton()

#endif // INCLUDED_THREADING_TASKMANAGER
