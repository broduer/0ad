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

#include "precompiled.h"

#include "ps/Input.h"

#include "lib/debug.h"
#include "ps/Profile.h"
#include "ps/TouchInput.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Conversions.h"
#include "scriptinterface/Request.h"

#include <SDL3/SDL_events.h>
#include <utility>

namespace Input
{
Manager::Manager() :
	m_PriorityEvents{std::make_unique<std::queue<SDL_Event>>()}
{
}

Manager::~Manager() = default;

void Manager::PushPriorityEvent(const SDL_Event& event)
{
	m_PriorityEvents->push(event);
}

void Manager::DispatchEvent(const SDL_Event& event)
{
	// Looks like std::find_if, but std::find_if does not guarantee the order of the handlers.
	for (const auto handler : m_Handlers)
	{
		if (handler && (*handler)(event) == Reaction::HANDLED)
			return;
	}
}

Manager::PollEventsResult::EventIterator::EventIterator(PollEventsResult& range) :
	m_Storage{&range}
{
	++*this;
}

Manager::PollEventsResult::EventIterator::reference Manager::PollEventsResult::EventIterator::operator*()
{
	return *m_Storage->m_Event;
}

Manager::PollEventsResult::EventIterator::pointer Manager::PollEventsResult::EventIterator::operator->()
{
	return &**this;
}

Manager::PollEventsResult::EventIterator& Manager::PollEventsResult::EventIterator::operator++()
{
	std::queue<SDL_Event>& priorityEvents{m_Storage->m_PriorityEvents};
	if (!priorityEvents.empty())
	{
		*m_Storage->m_Event = priorityEvents.front();
		priorityEvents.pop();
	}
	else if (!SDL_PollEvent(m_Storage->m_Event.get()))
		m_Storage = nullptr;

	return *this;
}

Manager::PollEventsResult::EventIterator& Manager::PollEventsResult::EventIterator::operator++(int)
{
	++(*this);
	return *this;
}

bool Manager::PollEventsResult::EventIterator::operator==(const EventIterator& other) const
{
	return m_Storage == other.m_Storage;
}

Manager::PollEventsResult::PollEventsResult(std::queue<SDL_Event>& priorityEvents) :
	m_PriorityEvents{priorityEvents},
	m_Event{std::make_unique<SDL_Event>()}
{
}

Manager::PollEventsResult::EventIterator Manager::PollEventsResult::begin()
{
	return EventIterator{*this};
}

Manager::PollEventsResult::EventIterator Manager::PollEventsResult::end()
{
	return EventIterator{};
}

Manager::PollEventsResult Manager::PollEvents()
{
	return PollEventsResult{*m_PriorityEvents};
}


HandlerBase::HandlerBase(HandlerBase*& pos) noexcept :
	m_ToReset{pos}
{
	const auto old = std::exchange(m_ToReset, this);
	ENSURE(old == nullptr && "There is already a handler registered to this slot.");
}

HandlerBase::~HandlerBase()
{
	const auto old = std::exchange(m_ToReset, nullptr);
	ENSURE(old == this && "No handler is registered to this slot.");
}
} // namespace Input
