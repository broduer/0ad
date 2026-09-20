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

#include "lib/self_test.h"

#include "ps/memory/AllocatorAdapter.h"
#include "ps/memory/LinearAllocator.h"

#include <cstddef>
#include <map>
#include <scoped_allocator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class TestAllocatorAdapter : public CxxTest::TestSuite
{
public:
	template<typename T>
	using Scoped = std::scoped_allocator_adaptor<
		PS::Memory::AllocatorAdapter<T, PS::Memory::LinearAllocator>>;

	void test_move()
	{
		PS::Memory::LinearAllocator allocator{16, 1024};

		std::vector<int, PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>> v1{
			PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>{allocator}};
		v1.assign(10, 1);

		std::vector<int, PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>> v2{
			PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>{allocator}};

		const std::size_t allocatedSize{allocator.GetSize()};
		v2 = std::move(v1);
		TS_ASSERT_EQUALS(allocatedSize, allocator.GetSize());
	}

	void test_swap()
	{
		PS::Memory::LinearAllocator allocator{16, 1024};

		std::vector<int, PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>> v1{
			PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>{allocator}};
		v1.assign(10, 1);

		std::vector<int, PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>> v2{
			PS::Memory::AllocatorAdapter<int, PS::Memory::LinearAllocator>{allocator}};

		const std::size_t allocatedSize{allocator.GetSize()};
		std::swap(v1, v2);
		TS_ASSERT_EQUALS(allocatedSize, allocator.GetSize());
	}

	void test_nested_containers()
	{
		PS::Memory::LinearAllocator allocator{16, 4096};

		using Vector = std::vector<int, Scoped<int>>;
		using Map = std::map<int, Vector, std::less<>, Scoped<std::pair<const int, Vector>>>;

		Map complexContainer(Map::allocator_type{allocator});
		complexContainer[42].assign(10, 1);
		TS_ASSERT_EQUALS(complexContainer.size(), 1);
		TS_ASSERT_EQUALS(complexContainer[42].size(), 10);
		TS_ASSERT_EQUALS(complexContainer[42][0], 1);
	}
};
