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

#ifndef INCLUDED_PS_ALLOCATORADAPTER
#define INCLUDED_PS_ALLOCATORADAPTER

#include <cstddef>
#include <memory>
#include <type_traits>

namespace PS::Memory
{

// Provides a way to use untyped allocators like LinearAllocator for
// containers. This allocator adapter must not outlive the target
// allocator. A client must not mix adapters with different
// allocators.
// @see LinearAllocator
template<typename T, typename Allocator>
class AllocatorAdapter
{
public:
	using value_type = T;

	// We don't need runtime checks and per-element copies for containers
	// when we have a unique allocators (means no mixing different allocators).
	using propagate_on_container_copy_assignment = std::true_type;
	using propagate_on_container_move_assignment = std::true_type;
	using propagate_on_container_swap = std::true_type;
	using is_always_equal = std::false_type;

	template<typename OtherT>
	struct rebind
	{
		using other = AllocatorAdapter<OtherT, Allocator>;
	};

	constexpr AllocatorAdapter(Allocator& allocator) noexcept : m_Allocator(std::addressof(allocator)) {}
	template<typename OtherT>
	constexpr AllocatorAdapter(const AllocatorAdapter<OtherT, Allocator>& allocatorAdapter) noexcept : m_Allocator(allocatorAdapter.m_Allocator) {}

	[[nodiscard]] constexpr T* allocate(std::size_t n)
	{
		return static_cast<T*>(m_Allocator->allocate(n * sizeof(T), alignof(T)));
	}

	constexpr void deallocate(T* ptr, std::size_t n)
	{
		m_Allocator->deallocate(static_cast<void*>(ptr), n * sizeof(T));
	}

	template<typename OtherT>
	constexpr bool operator==(const AllocatorAdapter<OtherT, Allocator>& other) const noexcept
	{
		return m_Allocator == other.m_Allocator;
	}

private:
	template<typename OtherT, typename OtherAllocator> friend class AllocatorAdapter;

	// A pointer to make it moveable.
	Allocator* m_Allocator{nullptr};
};

} // namespace PS::Memory

#endif // INCLUDED_PS_ALLOCATORADAPTER
