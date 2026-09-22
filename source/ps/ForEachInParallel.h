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

#include "ps/Future.h"
#include "ps/TaskManager.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace Threading
{

/**
 * Run @p work(i) for i in [0, count) on the calling thread and up to
 * MAX_HELPERS workers, in batches.
 */
template<typename Work>
void ForEachInParallel(const size_t count, const size_t batch, Work&& work)
{
	std::atomic<size_t> next{0};
	const auto run = [&next, count, batch, &work]() {
		for (;;)
		{
			const size_t begin = next.fetch_add(batch, std::memory_order_relaxed);
			if (begin >= count)
				return;
			const size_t end = std::min(begin + batch, count);
			for (size_t i = begin; i < end; ++i)
				work(i);
		}
	};
	// More workers cost more to wake than they save.
	constexpr size_t MAX_HELPERS = 12;
	const size_t helpers = count < 4 * batch ? 0 : std::min<size_t>(
		std::min(MAX_HELPERS, g_TaskManager.GetNumberOfWorkers()), (count + batch - 1) / batch - 1);
	std::vector<Future<void>> futures;
	futures.reserve(helpers);
	for (size_t i = 0; i < helpers; ++i)
		futures.emplace_back(g_TaskManager, run);
	run();
	for (Future<void>& future : futures)
		future.Get();
}

} // namespace Threading

#endif // INCLUDED_FOREACHINPARALLEL
