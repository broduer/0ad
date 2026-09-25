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

#ifndef INCLUDED_RENDERER_FIRSTAPPEARANCE
#define INCLUDED_RENDERER_FIRSTAPPEARANCE

#include "lib/allocators/STLAllocators.h"
#include "ps/memory/LinearAllocator.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>

/**
 * Rank keys by first appearance. Unlike sorting by address, sorting by rank
 * orders the same submissions identically in every run.
 */
template<typename Key, typename Hash = std::hash<Key>>
class CFirstAppearance
{
public:
	explicit CFirstAppearance(PS::Memory::ScopedLinearAllocator& allocator)
		: m_Ranks{0, Hash{}, std::equal_to<Key>{}, Allocator{allocator}}
	{
	}

	/**
	 * Return the number of distinct keys seen before @p key first appeared.
	 */
	std::uint32_t operator()(const Key& key)
	{
		return m_Ranks.try_emplace(key, static_cast<std::uint32_t>(m_Ranks.size())).first->second;
	}

private:
	using Allocator = ProxyAllocator<std::pair<const Key, std::uint32_t>, PS::Memory::ScopedLinearAllocator>;
	std::unordered_map<Key, std::uint32_t, Hash, std::equal_to<Key>, Allocator> m_Ranks;
};

#endif // INCLUDED_RENDERER_FIRSTAPPEARANCE
