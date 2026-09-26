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

#ifndef INCLUDED_FOREACHINPARALLEL
#define INCLUDED_FOREACHINPARALLEL

#include "ps/TaskManager.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>

namespace Threading
{

/**
 * Run @p work(i) for each i in [0, count), in batches of @p batch, on the calling thread and up
 * to MAX_HELPERS workers. Wait only for the batches workers took; a worker still queued when the
 * batches run out never touches @p work.
 */
template<typename Work>
void ForEachInParallel(const size_t count, const size_t batch, Work&& work)
{
	struct SProgress
	{
		std::atomic<size_t> next{0};
		std::atomic<size_t> done{0};
		std::mutex mutex;
		std::condition_variable finished;
	};
	// More workers cost more to wake than they save.
	constexpr size_t MAX_HELPERS = 12;
	const size_t helpers = count < 4 * batch ? 0 : std::min<size_t>(
		std::min(MAX_HELPERS, g_TaskManager.GetNumberOfWorkers()), (count + batch - 1) / batch - 1);
	if (helpers == 0)
	{
		for (size_t i = 0; i < count; ++i)
			work(i);
		return;
	}
	const std::shared_ptr<SProgress> progress{std::make_shared<SProgress>()};
	const auto run = [progress, count, batch, work = &work]() {
		for (;;)
		{
			const size_t begin = progress->next.fetch_add(batch, std::memory_order_relaxed);
			if (begin >= count)
				return;
			const size_t end = std::min(begin + batch, count);
			for (size_t i = begin; i < end; ++i)
				(*work)(i);
			if (progress->done.fetch_add(end - begin, std::memory_order_acq_rel) + (end - begin) == count)
			{
				// Notify under the lock so it can't fall between the caller's check and its wait.
				const std::lock_guard<std::mutex> lock{progress->mutex};
				progress->finished.notify_one();
			}
		}
	};
	// The first helper wakes the others, so the caller pays for one wake only.
	g_TaskManager.PushTask([run, helpers]() {
		for (size_t i = 1; i < helpers; ++i)
			g_TaskManager.PushTask(run, TaskPriority::HIGH);
		run();
	}, TaskPriority::HIGH);
	run();
	if (progress->done.load(std::memory_order_acquire) != count)
	{
		std::unique_lock<std::mutex> lock{progress->mutex};
		progress->finished.wait(lock, [&progress, count] {
			return progress->done.load(std::memory_order_acquire) == count;
		});
	}
}

} // namespace Threading

#endif // INCLUDED_FOREACHINPARALLEL
