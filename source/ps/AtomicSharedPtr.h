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

#ifndef INCLUDED_ATOMICSHAREDPTR
#define INCLUDED_ATOMICSHAREDPTR

#include <memory>
#include <mutex>
#include <utility>

namespace PS
{

/**
 * A shared_ptr that threads load, store and exchange concurrently, since not
 * every standard library provides std::atomic<std::shared_ptr>.
 */
template<typename T>
class AtomicSharedPtr
{
public:
	AtomicSharedPtr() = default;
	AtomicSharedPtr(const AtomicSharedPtr&) = delete;
	AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

	std::shared_ptr<T> load() const
	{
		std::lock_guard<std::mutex> lock{m_Mutex};
		return m_Ptr;
	}

	void store(std::shared_ptr<T> ptr)
	{
		// The old value is released after the lock.
		exchange(std::move(ptr));
	}

	std::shared_ptr<T> exchange(std::shared_ptr<T> ptr)
	{
		std::lock_guard<std::mutex> lock{m_Mutex};
		m_Ptr.swap(ptr);
		return ptr;
	}

private:
	mutable std::mutex m_Mutex;
	std::shared_ptr<T> m_Ptr;
};

} // namespace PS

#endif // INCLUDED_ATOMICSHAREDPTR
