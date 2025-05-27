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

#ifndef INCLUDED_DELTAARRAY
#define INCLUDED_DELTAARRAY

#include <unordered_map>

namespace JSI_MapEditor
{
	struct pair_hash
	{
		template <class T1, class T2>
		std::size_t operator () (const std::pair<T1, T2>& pair) const
		{
			std::size_t seed = 0;
			std::hash<ssize_t> sizeHash;

			seed ^= sizeHash(pair.first << 16) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= sizeHash(pair.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	template<typename T> class DeltaArray2D
	{
	public:
		virtual ~DeltaArray2D() {}

		T get(ssize_t x, ssize_t y);
		void set(ssize_t x, ssize_t y, const T& val);

		void OverlayWith(const DeltaArray2D<T>& overlayer);
		void Undo();
		void Redo();

	protected:
		virtual T getOld(ssize_t x, ssize_t y) = 0;
		virtual void setNew(ssize_t x, ssize_t y, const T& val) = 0;

	private:
		using Data = std::unordered_map<std::pair<ssize_t, ssize_t>, std::pair<T, T>, pair_hash>; // map of <x,y> -> <old_val, new_val>
		Data m_Data;
	};


	template<typename T>
	T DeltaArray2D<T>::get(ssize_t x, ssize_t y)
	{
		auto it = m_Data.find(std::make_pair(x, y));
		if (it == m_Data.end())
			return getOld(x, y);
		return it->second.second;
	}

	template<typename T>
	void DeltaArray2D<T>::set(ssize_t x, ssize_t y, const T& val)
	{
		auto it = m_Data.find(std::make_pair(x, y));
		if (it == m_Data.end())
			m_Data.insert(std::make_pair(std::make_pair(x, y), std::make_pair(getOld(x, y), val)));
		else
			it->second.second = val;
		setNew(x, y, val);
	}

	template<typename T>
	void DeltaArray2D<T>::OverlayWith(const DeltaArray2D<T>& overlayer)
	{
		for (const auto& [key, value] : overlayer.m_Data)
		{
			auto it2 = m_Data.find(key);
			if (it2 == m_Data.end())
				m_Data.insert({key, value});
			else
				it2->second.second = value.second;
		}
	}

	template <typename T>
	void DeltaArray2D<T>::Undo()
	{
		for (const auto& [key, value] : m_Data)
			setNew(key.first, key.second, value.first);
	}

	template <typename T>
	void DeltaArray2D<T>::Redo()
	{
		for (const auto& [key, value] : m_Data)
			setNew(key.first, key.second, value.second);
	}
}
#endif // INCLUDED_DELTAARRAY
