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

#ifndef INCLUDED_STOP_TOKEN
#define INCLUDED_STOP_TOKEN

#include <concepts>

class StopToken
{
public:
	explicit StopToken(const std::atomic<bool>& request) noexcept :
		m_Request{request}
	{}

	bool IsStopRequested() const noexcept
	{
		return m_Request.load();
	}
private:
	const std::atomic<bool>& m_Request;
};

// When it's invocable with a `StopToken` it must not be invocable with a `StopToken&`. Because it should
// always be passed around as value.
template<typename Func, typename... Args>
concept TokenInvocable = (std::invocable<Func, StopToken, Args...> || std::invocable<Func, Args...>) &&
	(std::invocable<Func, StopToken, Args...> || !std::invocable<Func, StopToken&, Args...>);

#endif // INCLUDED_STOP_TOKEN
