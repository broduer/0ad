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

#include "ps/Input.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <iterator>

class TestInput : public CxxTest::TestSuite
{
	static std::uint32_t GetEventType(const int numevents)
	{
		std::uint32_t eventType{SDL_RegisterEvents(numevents)};
		TS_ASSERT_DIFFERS(eventType, std::numeric_limits<std::uint32_t>::max());
		TS_ASSERT_STR_EQUALS(SDL_GetError(), "");
		return eventType;
	}

	static SDL_Event MakeEvent(const std::uint32_t eventType)
	{
		SDL_Event ev{};
		ev.type = eventType;
		return ev;
	}

	static void PushEvent(const std::uint32_t eventType)
	{
		SDL_Event ev{MakeEvent(eventType)};
		TS_ASSERT_EQUALS(SDL_PushEvent(&ev), 1);
		TS_ASSERT_STR_EQUALS(SDL_GetError(), "");
	}

	static void PushPriorityEvent(Input::Manager& manager, const std::uint32_t eventType)
	{
		manager.PushPriorityEvent(MakeEvent(eventType));
	}

public:
	void setUp()
	{
		SDL_Init(SDL_INIT_EVENTS);
	}

	void tearDown()
	{
		SDL_Quit();
	}

	void test_NoEvent()
	{
		Input::Manager manager;
		auto range = manager.PollEvents();
		TS_ASSERT_EQUALS(std::distance(range.begin(), range.end()), 0);
	}

	void test_Event()
	{
		Input::Manager manager;
		PushEvent(GetEventType(1));
		auto range = manager.PollEvents();
		TS_ASSERT_EQUALS(std::distance(range.begin(), range.end()), 1);
	}

	void test_PriorityEvent()
	{
		Input::Manager manager;
		PushPriorityEvent(manager, GetEventType(1));
		auto range = manager.PollEvents();
		TS_ASSERT_EQUALS(std::distance(range.begin(), range.end()), 1);
	}

	void test_PriorityOrder()
	{
		Input::Manager manager;
		const std::uint32_t eventTypeStart{SDL_RegisterEvents(2)};
		const std::uint32_t priorityEventType{eventTypeStart + 1};

		PushEvent(eventTypeStart);
		PushPriorityEvent(manager, priorityEventType);
		PushEvent(eventTypeStart);

		auto range = manager.PollEvents();
		auto iter = range.begin();
		TS_ASSERT_DIFFERS(iter, range.end());
		TS_ASSERT_EQUALS(iter->type, priorityEventType);
		++iter;
		TS_ASSERT_DIFFERS(iter, range.end());
		TS_ASSERT_EQUALS(iter->type, eventTypeStart);
		++iter;
		TS_ASSERT_DIFFERS(iter, range.end());
		TS_ASSERT_EQUALS(iter->type, eventTypeStart);
		++iter;
		TS_ASSERT_EQUALS(iter, range.end());
	}

	void test_Dispatch()
	{
		Input::Manager manager;
		bool triggered{false};
		Input::Handler _{manager, std::integral_constant<size_t, 0>{}, [&](const SDL_Event&){
			triggered = true;
			return Input::Reaction::HANDLED;
		}};

		TS_ASSERT(!triggered);
		manager.DispatchEvent(MakeEvent(GetEventType(1)));
		TS_ASSERT(triggered);
	}

	void test_DispatchFilter()
	{
		Input::Manager manager;
		const std::uint32_t eventTypeStart{SDL_RegisterEvents(2)};
		const std::uint32_t filteredEventType{eventTypeStart + 1};
		[[maybe_unused]] Input::Handler filter{manager, std::integral_constant<size_t, 0>{},
			[&](const SDL_Event& ev){
				return ev.type == filteredEventType ? Input::Reaction::HANDLED :
					Input::Reaction::PASS;
			}};

		bool triggered{false};
		[[maybe_unused]] Input::Handler test{manager, std::integral_constant<size_t, 1>{},
			[&](const SDL_Event&){
				triggered = true;
				return Input::Reaction::HANDLED;
			}};

		manager.DispatchEvent(MakeEvent(filteredEventType));
		TS_ASSERT(!triggered);
		manager.DispatchEvent(MakeEvent(eventTypeStart));
		TS_ASSERT(triggered);
	}

	void test_Unsubscribe()
	{
		Input::Manager manager;
		bool triggered{false};
		{
			Input::Handler _{manager, std::integral_constant<size_t, 0>{}, [&](const SDL_Event&){
				triggered = true;
				return Input::Reaction::HANDLED;
			}};
		}

		manager.DispatchEvent(MakeEvent(GetEventType(1)));
		TS_ASSERT(!triggered);
	}
};
