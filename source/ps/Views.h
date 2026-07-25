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

#ifndef PS_VIEWS_H
#define PS_VIEWS_H

#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <tuple>
#include <utility>

namespace PS::Views
{

namespace detail
{

// Generic helpers
template <template <typename> typename View, typename... Args>
struct ViewClosure
{
    std::tuple<Args...> m_Args;

	explicit constexpr ViewClosure(Args... args) : m_Args(std::move(args)...) {}

    template <std::ranges::range R>
    constexpr auto operator()(R&& base) const
	{
        return View<std::views::all_t<R>>(std::views::all(std::forward<R>(base)), std::get<Args>(m_Args)...);
    }
};

template <std::ranges::range R, template <typename> typename View, typename... Args>
constexpr auto operator|(R&& base, const ViewClosure<View, Args...>& closure) {
    return closure(std::forward<R>(base));
}

// Enumerate View
template <std::ranges::range R>
class EnumerateView : std::ranges::view_interface<EnumerateView<R>>
{
private:
    R m_Base;
    std::ptrdiff_t m_Offset = 0;

    class iterator
	{
	public:
        using value_type = std::pair<std::ptrdiff_t, std::ranges::range_reference_t<R>>;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        iterator() = default;
        iterator(std::ranges::iterator_t<R> it, std::ptrdiff_t idx)
            : m_Iter(std::move(it)), m_Index(idx) {}

        value_type operator*() const { return {m_Index, *m_Iter}; }

        iterator& operator++()
		{
            ++m_Iter;
            ++m_Index;
            return *this;
        }

        iterator operator++(int)
		{
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const iterator& other) const
		{
            return m_Iter == other.m_Iter;
        }

	private:
        std::ranges::iterator_t<R> m_Iter;
        std::ptrdiff_t m_Index{0};
    };

public:
    EnumerateView() = default;
    explicit EnumerateView(R base, std::ptrdiff_t offset = 0)
        : m_Base(std::move(base)), m_Offset(offset) {}

    iterator begin() { return {std::ranges::begin(m_Base), m_Offset}; }
    iterator end() { return {std::ranges::end(m_Base), 0}; }

    iterator begin() const { return {std::ranges::begin(m_Base), m_Offset}; }
    iterator end() const { return {std::ranges::end(m_Base), 0}; }
};

struct EnumerateFunctor
{
    constexpr auto operator()(std::ptrdiff_t offset = 0) const
	{
        return ViewClosure<EnumerateView, std::ptrdiff_t>{offset};
    }

    template <std::ranges::range R>
    constexpr auto operator()(R&& base, std::ptrdiff_t offset = 0) const
	{
        return EnumerateView<std::views::all_t<R>>(std::views::all(std::forward<R>(base)), offset);
    }
};

} // namespace detail

/**
 * Enumerate
 *
 * View inspired by pythons enumerate
 */
inline constexpr detail::EnumerateFunctor Enumerate{};

} // namespace PS::Views

#endif // PS_VIEWS_H
