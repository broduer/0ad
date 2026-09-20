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

#include "ps/ThreadAffinity.h"
#include "ps/Threading.h"

#if OS_LINUX
#include <pthread.h>
#include <sched.h>
#endif

class TestThreadAffinity : public CxxTest::TestSuite
{
#if OS_LINUX
	cpu_set_t m_Original;

	cpu_set_t GetAffinity()
	{
		cpu_set_t affinity{};
		TS_ASSERT_EQUALS(pthread_getaffinity_np(pthread_self(), sizeof(affinity), &affinity), 0);
		return affinity;
	}
#endif

public:
	void setUp()
	{
#if OS_LINUX
		m_Original = GetAffinity();
#endif
	}

	void tearDown()
	{
#if OS_LINUX
		ThreadAffinity::RestoreCurrentThreadAffinity();
		TS_ASSERT_EQUALS(pthread_setaffinity_np(pthread_self(), sizeof(m_Original), &m_Original), 0);
#endif
	}

	void test_thread_creation_and_restore()
	{
#if OS_LINUX
		ThreadAffinity::PinCurrentThreadToRenderCpu();
		const cpu_set_t pinned = GetAffinity();
		cpu_set_t inherited{};
		int status = -1;
		auto thread = Threading::CreateThread([&]() {
			status = pthread_getaffinity_np(pthread_self(), sizeof(inherited), &inherited);
		});
		thread.join();
		TS_ASSERT_EQUALS(status, 0);
		TS_ASSERT(CPU_EQUAL(&inherited, &m_Original));
		const cpu_set_t afterCreation = GetAffinity();
		TS_ASSERT(CPU_EQUAL(&afterCreation, &pinned));
		ThreadAffinity::RestoreCurrentThreadAffinity();
		const cpu_set_t restored = GetAffinity();
		TS_ASSERT(CPU_EQUAL(&restored, &m_Original));
#endif
	}

	void test_pin_respects_allowed_cpus()
	{
#if OS_LINUX
		// Initialize core selection before restricting this thread further.
		ThreadAffinity::PinCurrentThreadToRenderCpu();
		const cpu_set_t pinned = GetAffinity();
		cpu_set_t intersection;
		CPU_AND(&intersection, &pinned, &m_Original);
		TS_ASSERT(CPU_EQUAL(&intersection, &pinned));
		ThreadAffinity::RestoreCurrentThreadAffinity();

		// Prefer a CPU outside the selected render core's mask.
		int selected = -1;
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
			if (CPU_ISSET(cpu, &m_Original) &&
				(selected < 0 || !CPU_ISSET(cpu, &pinned)))
				selected = cpu;
		TS_ASSERT_DIFFERS(selected, -1);
		if (selected < 0)
			return;
		cpu_set_t restricted;
		CPU_ZERO(&restricted);
		CPU_SET(selected, &restricted);
		TS_ASSERT_EQUALS(pthread_setaffinity_np(pthread_self(), sizeof(restricted), &restricted), 0);
		ThreadAffinity::PinCurrentThreadToRenderCpu();
		const cpu_set_t afterPin = GetAffinity();
		TS_ASSERT(CPU_EQUAL(&afterPin, &restricted));
#endif
	}
};
