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

#ifndef INCLUDED_PS_STATICCIRCULARBUFFER
#define INCLUDED_PS_STATICCIRCULARBUFFER

#include "ps/containers/StaticVector.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <compare>
#include <fmt/format.h>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace PS
{

/**
 * A fixed-capacity circular buffer optimized for path history tracking.
 * Unlike StaticVector, this container automatically overwrites the oldest elements
 * when full, making it ideal for tracking recent movement history.
 *
 * The buffer maintains a logical circular structure where:
 * - Elements are stored contiguously in memory
 * - The "start" index moves forward when the buffer is full
 * - Random access is O(1) with index wrapping
 * - Iteration is O(N) and follows chronological order (oldest to newest)
 */
template<typename T, size_t N>
class StaticCircularBuffer
{
	static_assert(N > 0, "StaticCircularBuffer capacity must be greater than 0");
	static_assert(std::is_nothrow_destructible_v<T>);

public:
	using value_type = T;
	using size_type = decltype(MakeSmallestCapableUnsigned<N>());
	using difference_type = decltype(MakeSmallestCapableSigned<N>());
	using reference = value_type&;
	using const_reference = const value_type&;
	using pointer = value_type*;
	using const_pointer = const value_type*;

	// Combined iterator template that handles both iterator and const_iterator
	template<bool IsConst>
	class iterator_template;

	using iterator = iterator_template<false>;
	using const_iterator = iterator_template<true>;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	StaticCircularBuffer() noexcept
		: m_Start{0}
		, m_Size{0}
	{
	}

	~StaticCircularBuffer()
	{
		clear();
	}

	StaticCircularBuffer(const StaticCircularBuffer& other) noexcept(
		std::is_nothrow_copy_constructible_v<T>)
		: m_Start{other.m_Start}
		, m_Size{other.m_Size}
	{
		// Copy elements using placement new
		for (size_type i{0}; i < m_Size; ++i)
		{
			size_type idx = wrap_offset(m_Start, i);
			::new(static_cast<void*>(&m_Storage[idx])) T(other.get(idx));
		}
	}

	StaticCircularBuffer& operator=(const StaticCircularBuffer& other) noexcept(
		std::is_nothrow_copy_constructible_v<T>)
	{
		if (this != &other)
		{
			clear();
			m_Start = other.m_Start;
			m_Size = other.m_Size;

			for (size_type i{0}; i < m_Size; ++i)
			{
				size_type idx = wrap_offset(m_Start, i);
				::new(static_cast<void*>(&m_Storage[idx])) T(other.get(idx));
			}
		}
		return *this;
	}

	StaticCircularBuffer(StaticCircularBuffer&& other) noexcept(
		std::is_nothrow_move_constructible_v<T>)
		: m_Start{other.m_Start}
		, m_Size{other.m_Size}
	{
		// Move elements using placement new
		for (size_type i{0}; i < m_Size; ++i)
		{
			size_type idx = wrap_offset(m_Start, i);
			::new(static_cast<void*>(&m_Storage[idx])) T(std::move(other.get(idx)));
			other.get(idx).~T();
		}
		other.m_Size = 0;
		other.m_Start = 0;
	}

	StaticCircularBuffer& operator=(StaticCircularBuffer&& other) noexcept(
		std::is_nothrow_move_constructible_v<T>)
	{
		if (this != &other)
		{
			clear();
			m_Start = other.m_Start;
			m_Size = other.m_Size;

			for (size_type i{0}; i < m_Size; ++i)
			{
				size_type idx = wrap_offset(m_Start, i);
				::new(static_cast<void*>(&m_Storage[idx])) T(std::move(other.get(idx)));
				other.get(idx).~T();
			}
			other.m_Size = 0;
			other.m_Start = 0;
		}
		return *this;
	}

	/**
	* Adds an element to the end of the buffer.
	* If the buffer is full, the oldest element is overwritten.
	* Returns a reference to the added element.
	*/
	reference push_back(const T& value)
	{
		return emplace_back(value);
	}

	/**
	* Adds an element to the end of the buffer.
	* If the buffer is full, the oldest element is overwritten.
	* Returns a reference to the added element.
	*/
	reference push_back(T&& value)
	{
		return emplace_back(std::move(value));
	}

	/**
	* Constructs an element at the end of the buffer.
	* If the buffer is full, the oldest element is overwritten.
	* Returns a reference to the constructed element.
	*/
	template<typename... Args>
	reference emplace_back(Args&&... args)
	{
		size_type writeIdx{};

		if (m_Size == N)
		{
			// Buffer is full, overwrite oldest
			writeIdx = m_Start;
			get(writeIdx).~T();
			::new(static_cast<void*>(&m_Storage[writeIdx])) T(std::forward<Args>(args)...);
			m_Start = wrap_increment(m_Start);
		}
		else
		{
			// Buffer has space, append
			writeIdx = wrap_offset(m_Start, m_Size);
			::new(static_cast<void*>(&m_Storage[writeIdx])) T(std::forward<Args>(args)...);
			++m_Size;
		}

		return get(writeIdx);
	}

	/**
	 * Removes the oldest element from the buffer.
	 * Throws if buffer is empty.
	 */
	void pop_front()
	{
		if (empty())
			throw std::out_of_range{"Called pop_front on empty StaticCircularBuffer"};

		get(m_Start).~T();
		m_Start = wrap_increment(m_Start);
		--m_Size;
	}

	/**
	 * Removes the newest element from the buffer.
	 * Throws if buffer is empty.
	 */
	void pop_back()
	{
		if (empty())
			throw std::out_of_range{"Called pop_back on empty StaticCircularBuffer"};

		size_type lastIdx = wrap_offset(m_Start, m_Size - 1);
		get(lastIdx).~T();
		--m_Size;
	}

	/**
	* Accesses element at logical index (0 = oldest, size()-1 = newest).
	*/
	reference operator[](size_type logicalIndex)
	{
		ASSERT(logicalIndex < m_Size);
		size_type physicalIdx = wrap_offset(m_Start, logicalIndex);
		return get(physicalIdx);
	}

	/**
	* Accesses element at logical index (0 = oldest, size()-1 = newest).
	*/
	const_reference operator[](size_type logicalIndex) const
	{
		ASSERT(logicalIndex < m_Size);
		size_type physicalIdx = wrap_offset(m_Start, logicalIndex);
		return get(physicalIdx);
	}

	/**
	 * Accesses element at logical index with bounds checking.
	 */
	reference at(size_type logicalIndex)
	{
		if (logicalIndex >= m_Size)
			throw std::out_of_range{fmt::format(
				"StaticCircularBuffer::at: index {} out of range (size={})",
				logicalIndex, m_Size)};

		return (*this)[logicalIndex];
	}

	/**
	 * Accesses element at logical index with bounds checking.
	 */
	const_reference at(size_type logicalIndex) const
	{
		if (logicalIndex >= m_Size)
			throw std::out_of_range{fmt::format(
				"StaticCircularBuffer::at: index {} out of range (size={})",
				logicalIndex, m_Size)};

		return (*this)[logicalIndex];
	}

	/**
	 * Returns the oldest element in the buffer.
	 */
	reference front()
	{
		ASSERT(!empty());
		return get(m_Start);
	}

	/**
	 * Returns the oldest element in the buffer.
	 */
	const_reference front() const
	{
		ASSERT(!empty());
		return get(m_Start);
	}

	/**
	 * Returns the newest element in the buffer.
	 */
	reference back()
	{
		ASSERT(!empty());
		size_type lastIdx = wrap_offset(m_Start, m_Size - 1);
		return get(lastIdx);
	}

	/**
	 * Returns the newest element in the buffer.
	 */
	const_reference back() const
	{
		ASSERT(!empty());
		size_type lastIdx = wrap_offset(m_Start, m_Size - 1);
		return get(lastIdx);
	}

	iterator begin() noexcept
	{
		return iterator{this, 0};
	}

	const_iterator begin() const noexcept
	{
		return const_iterator{this, 0};
	}

	const_iterator cbegin() const noexcept
	{
		return const_iterator{this, 0};
	}

	iterator end() noexcept
	{
		return iterator{this, m_Size};
	}

	const_iterator end() const noexcept
	{
		return const_iterator{this, m_Size};
	}

	const_iterator cend() const noexcept
	{
		return const_iterator{this, m_Size};
	}

	reverse_iterator rbegin() noexcept
	{
		return reverse_iterator{end()};
	}

	const_reverse_iterator rbegin() const noexcept
	{
		return const_reverse_iterator{end()};
	}

	const_reverse_iterator crbegin() const noexcept
	{
		return const_reverse_iterator{cend()};
	}

	reverse_iterator rend() noexcept
	{
		return reverse_iterator{begin()};
	}

	const_reverse_iterator rend() const noexcept
	{
		return const_reverse_iterator{begin()};
	}

	const_reverse_iterator crend() const noexcept
	{
		return const_reverse_iterator{cbegin()};
	}

	/**
	 * Returns true if the buffer is empty.
	 */
	bool empty() const noexcept
	{
		return m_Size == 0;
	}

	/**
	 * Returns true if the buffer is full (next push will overwrite oldest).
	 */
	bool full() const noexcept
	{
		return m_Size == N;
	}

	/**
	 * Returns the number of elements currently stored.
	 */
	size_type size() const noexcept
	{
		return m_Size;
	}

	/**
	 * Returns the maximum number of elements that can be stored.
	 */
	constexpr size_type capacity() const noexcept
	{
		return N;
	}

	/**
	 * Removes all elements from the buffer.
	 */
	void clear() noexcept
	{
		if (!empty())
		{
			size_type first_count = std::min(m_Size, static_cast<size_type>(N - m_Start));
			std::ranges::destroy_n(&get(m_Start), first_count);

			size_type second_count = m_Size - first_count;
			if (second_count > 0)
				std::ranges::destroy_n(&get(0), second_count);
		}

		m_Size = 0;
		m_Start = 0;
	}

private:
	// Using std::aligned_storage to avoid requiring T to be default constructible
	using Storage = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
	std::array<Storage, N> m_Storage;

	size_type m_Start{0};  // Index of the oldest element
	size_type m_Size{0};   // Number of elements currently stored

	// Helper methods for safe modulo arithmetic with type conversion
	size_type wrap_index(size_type idx) const noexcept
	{
		return static_cast<size_type>(idx % N);
	}

	size_type wrap_offset(size_type start, size_type offset) const noexcept
	{
		return static_cast<size_type>((start + offset) % N);
	}

	size_type wrap_add(size_type a, size_type b) const noexcept
	{
		return static_cast<size_type>((a + b) % N);
	}

	size_type wrap_sub(size_type a, size_type b) const noexcept
	{
		// Handle wrap-around for subtraction: (a - b) mod N
		return static_cast<size_type>((a + N - b) % N);
	}

	size_type wrap_increment(size_type idx) const noexcept
	{
		return static_cast<size_type>((idx + 1) % N);
	}

	size_type wrap_decrement(size_type idx) const noexcept
	{
		return static_cast<size_type>((idx + N - 1) % N);
	}

	// Helper methods to access storage as T with std::launder
	T& get(size_type idx) noexcept
	{
		// std::launder tells compiler about type punning
		return *std::launder(reinterpret_cast<T*>(&m_Storage[idx]));
	}

	const T& get(size_type idx) const noexcept
	{
		return *std::launder(reinterpret_cast<const T*>(&m_Storage[idx]));
	}
};

// Combined iterator implementation
template<typename T, size_t N>
template<bool IsConst>
class StaticCircularBuffer<T, N>::iterator_template
{
private:
	// Friend declarations for iterator interoperability
	friend class iterator_template<true>;  // const iterator can access non-const iterator's members
	friend class StaticCircularBuffer<T, N>;

	// Choose pointer type based on constness
	using BufferPtr = std::conditional_t<IsConst, const StaticCircularBuffer*, StaticCircularBuffer*>;
	using ElementType = std::conditional_t<IsConst, const T, T>;

public:
	// Standard iterator traits
	using iterator_category = std::random_access_iterator_tag;
	using value_type = ElementType;
	using difference_type = typename StaticCircularBuffer::difference_type;
	using pointer = ElementType*;
	using reference = ElementType&;

	// Default constructor
	iterator_template() noexcept = default;

	// Constructor - SFINAE ensures correct constructor is used
	template<bool C = IsConst, typename = std::enable_if_t<!C>>
	iterator_template(StaticCircularBuffer* buffer, size_type logicalIndex) noexcept
		: m_Buffer(buffer)
		, m_LogicalIndex(logicalIndex)
	{
	}

	// Constructor for const iterator
	template<bool C = IsConst, typename = std::enable_if_t<C>>
	iterator_template(const StaticCircularBuffer* buffer, size_type logicalIndex) noexcept
		: m_Buffer(buffer)
		, m_LogicalIndex(logicalIndex)
	{
	}

	// Conversion constructor: non-const iterator -> const iterator
	template<bool C = IsConst, typename = std::enable_if_t<C>>
	iterator_template(const iterator_template<false>& other) noexcept
		: m_Buffer(other.m_Buffer)
		, m_LogicalIndex(other.m_LogicalIndex)
	{
	}

	// Dereference operators
	reference operator*() const noexcept
	{
		return (*m_Buffer)[m_LogicalIndex];
	}

	pointer operator->() const noexcept
	{
		return &(*m_Buffer)[m_LogicalIndex];
	}

	// Increment/decrement operators
	iterator_template& operator++() noexcept
	{
		++m_LogicalIndex;
		return *this;
	}

	iterator_template operator++(int) noexcept
	{
		iterator_template tmp{*this};
		++(*this);
		return tmp;
	}

	iterator_template& operator--() noexcept
	{
		--m_LogicalIndex;
		return *this;
	}

	iterator_template operator--(int) noexcept
	{
		iterator_template tmp{*this};
		--(*this);
		return tmp;
	}

	// Arithmetic operators
	iterator_template& operator+=(difference_type n) noexcept
	{
		m_LogicalIndex += n;
		return *this;
	}

	iterator_template& operator-=(difference_type n) noexcept
	{
		m_LogicalIndex -= n;
		return *this;
	}

	iterator_template operator+(difference_type n) const noexcept
	{
		iterator_template tmp{*this};
		tmp += n;
		return tmp;
	}

	friend iterator_template operator+(difference_type n, const iterator_template& it) noexcept
	{
		return it + n;
	}

	iterator_template operator-(difference_type n) const noexcept
	{
		iterator_template tmp{*this};
		tmp -= n;
		return tmp;
	}

	difference_type operator-(const iterator_template& other) const noexcept
	{
		return m_LogicalIndex - other.m_LogicalIndex;
	}

	// Subscript operator
	reference operator[](difference_type n) const noexcept
	{
		return *(*this + n);
	}

	// Three-way comparison operator
	std::partial_ordering operator<=>(const iterator_template& other) const noexcept
	{
		if (m_Buffer != other.m_Buffer)
			return std::partial_ordering::unordered;

		// Automatically converts strong_ordering to partial_ordering
		return m_LogicalIndex <=> other.m_LogicalIndex;
	}

	// Equality operator (still needed since <=> doesn't automatically generate == in C++20)
	bool operator==(const iterator_template& other) const noexcept
	{
		return m_Buffer == other.m_Buffer && m_LogicalIndex == other.m_LogicalIndex;
	}

private:
	BufferPtr m_Buffer{nullptr};
	size_type m_LogicalIndex{0};
};

} // namespace PS

#endif // INCLUDED_PS_STATICCIRCULARBUFFER