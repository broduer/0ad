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

#include "lib/self_test.h"

#include "ps/Future.h"
#include "ps/execution/Batch.h"

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>

class TestExecutionBatch : public CxxTest::TestSuite
{
public:
	struct TestTaskManager
	{
		std::vector<std::function<void()>> tasks;
		void PushTask(std::function<void()> task)
		{
			tasks.push_back(std::move(task));
		}
	};

	void test_empty()
	{
		bool executed{false};
		TestTaskManager ttm;
		const std::array<int, 0> input{};
		Future noret{ttm, PS::Batch{input, [&](int){ executed = true; }}};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 0);
		TS_ASSERT(noret.IsDone())
		TS_ASSERT(!executed);
	}

	void test_single()
	{
		bool executed{false};
		TestTaskManager ttm;
		const auto input = std::to_array({0});
		Future noret{ttm, PS::Batch{input, [&](int){ executed = true; }}};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();
		TS_ASSERT(executed);
	}

	void test_r_value()
	{
		bool executed{false};
		TestTaskManager ttm;
		Future noret{ttm, PS::Batch{std::to_array({0}), [&](int){ executed = true; }}};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 1);
		std::exchange(ttm.tasks, {})[0]();
		TS_ASSERT(executed);
	}

	void test_count_iota()
	{
		// When it's actually distributed across threads it should be attomic.
		std::int8_t sum{0};
		TestTaskManager ttm;
		Future noret{ttm, PS::Batch{std::ranges::iota_view(0, 6), [&](int){ ++sum; }}};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 6);
		std::ranges::for_each(std::exchange(ttm.tasks, {}), [](auto& func){ func(); });
		TS_ASSERT_EQUALS(sum, 6);
	}

	void test_l_value()
	{
		std::int8_t index{0};
		TestTaskManager ttm;
		auto currentIndices = std::to_array<std::int8_t>({0, 0, 0, 0, 0, 0});
		Future noret{ttm, PS::Batch{currentIndices, [&](std::int8_t& currentIndex)
			{
				currentIndex = index++;
			}}};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 6);
		std::ranges::for_each(std::exchange(ttm.tasks, {}), [](auto& func){ func(); });
		// When it's actually distributed across threads the order is arbitrary.
		TS_ASSERT_EQUALS(currentIndices, std::to_array<std::int8_t>({0, 1, 2, 3, 4, 5}));
	}

	void test_input_range()
	{
		struct InputRange
		{
			struct iterator
			{
				using difference_type = std::ptrdiff_t;
				using value_type = int;
				int operator *() const
				{
					return 0;
				}

				iterator& operator++()
				{
					return *this;
				}

				iterator operator++(int)
				{
					return {};
				}

				bool operator ==(const std::default_sentinel_t&) const
				{
					return true;
				}
			};

			std::size_t size() const noexcept
			{
				return 0;
			}

			iterator begin() const noexcept
			{
				return {};
			}

			std::default_sentinel_t end() const noexcept
			{
				return std::default_sentinel;
			}
		};

		bool executed{false};
		TestTaskManager ttm;
		const InputRange input{};
		Future noret{ttm, PS::Batch{input, [&](int){ executed = true; }}};
		TS_ASSERT(!executed);
	}

	void test_exception()
	{
		std::int8_t sum{0};
		TestTaskManager ttm;
		Future noret{ttm, PS::Batch{std::ranges::iota_view(0, 6), [&](int)
			{
				++sum;
				throw 0;
			}}};
		TS_ASSERT_EQUALS(ttm.tasks.size(), 6);
		std::ranges::for_each(std::exchange(ttm.tasks, {}), [](auto& func){ func(); });
		TS_ASSERT_EQUALS(sum, 1);
	}
};
