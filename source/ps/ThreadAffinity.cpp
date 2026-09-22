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

#include "ThreadAffinity.h"

#if OS_LINUX
#include <fstream>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <utility>
#include <vector>
#endif

namespace ThreadAffinity
{

#if OS_LINUX

namespace
{
struct RenderCore
{
	int cpu = -1;
	cpu_set_t siblings{};
	// The CPUs sharing the render core's last-level cache, siblings excluded.
	cpu_set_t neighbours{};
};

thread_local bool g_Pinned = false;
thread_local cpu_set_t g_OriginalAffinity;

// Parse a sysfs CPU list such as "8-15,24-31".
void ParseCpuList(const std::string& list, cpu_set_t& set)
{
	CPU_ZERO(&set);
	size_t position = 0;
	while (position < list.size())
	{
		size_t end = list.find(',', position);
		if (end == std::string::npos)
			end = list.size();
		const std::string range = list.substr(position, end - position);
		const size_t dash = range.find('-');
		const int first = atoi(range.c_str());
		const int last = dash == std::string::npos ? first : atoi(range.c_str() + dash + 1);
		for (int cpu = first; cpu >= 0 && cpu <= last && cpu < CPU_SETSIZE; ++cpu)
			CPU_SET(cpu, &set);
		position = end + 1;
	}
}

// Return the platform's preferred-core ranking of the CPU, or 0 if it has none.
int GetPreferredCoreRanking(const int cpu)
{
	const std::string directory = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
	for (const char* name : {"/cpufreq/amd_pstate_prefcore_ranking", "/acpi_cppc/highest_perf"})
	{
		std::ifstream file{directory + name};
		int ranking = 0;
		if (file >> ranking)
			return ranking;
	}
	return 0;
}

const RenderCore& GetRenderCore()
{
	static const RenderCore core = []() {
		RenderCore result;
		// Respect taskset/cpuset restrictions, including sparse CPU numbering.
		cpu_set_t allowed;
		if (pthread_getaffinity_np(pthread_self(), sizeof(allowed), &allowed) != 0)
			return result;
		if (CPU_COUNT(&allowed) < 8)
			return result;

		// Stay in the cache domain of the last physical core, and use the
		// first hardware thread of the highest ranked core there.
		std::vector<std::pair<int, int>> coreIds;
		int lastCpu = -1;
		int lastCore = -1;
		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
		{
			if (!CPU_ISSET(cpu, &allowed))
				continue;
			std::ifstream file{"/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id"};
			int coreId = -1;
			if (!(file >> coreId))
				return RenderCore{};
			coreIds.emplace_back(cpu, coreId);
			if (coreId > lastCore)
			{
				lastCore = coreId;
				lastCpu = cpu;
			}
		}
		if (lastCpu < 0)
			return result;

		cpu_set_t domain = allowed;
		std::string list;
		std::ifstream shared{"/sys/devices/system/cpu/cpu" + std::to_string(lastCpu) +
			"/cache/index3/shared_cpu_list"};
		if (shared >> list)
		{
			ParseCpuList(list, result.neighbours);
			CPU_AND(&domain, &allowed, &result.neighbours);
		}

		int bestRanking = -1;
		int bestCore = -1;
		for (const auto& [cpu, coreId] : coreIds)
		{
			if (!CPU_ISSET(cpu, &domain))
				continue;
			const int ranking = GetPreferredCoreRanking(cpu);
			if (ranking > bestRanking || (ranking == bestRanking && coreId > bestCore))
			{
				bestRanking = ranking;
				bestCore = coreId;
				result.cpu = cpu;
			}
		}
		if (result.cpu < 0)
			return RenderCore{};

		// core_id is not globally unique on machines with multiple packages.
		std::ifstream siblings{"/sys/devices/system/cpu/cpu" + std::to_string(result.cpu) +
			"/topology/thread_siblings_list"};
		if (!(siblings >> list))
			return RenderCore{};
		ParseCpuList(list, result.siblings);

		for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
			if (CPU_ISSET(cpu, &result.siblings))
				CPU_CLR(cpu, &result.neighbours);
		return result;
	}();
	return core;
}
} // anonymous namespace

void PinCurrentThreadToRenderCpu()
{
	if (g_Pinned)
		return;
	const RenderCore& core = GetRenderCore();
	if (core.cpu < 0)
		return;
	cpu_set_t original;
	if (pthread_getaffinity_np(pthread_self(), sizeof(original), &original) != 0 ||
		!CPU_ISSET(core.cpu, &original))
	{
		return;
	}
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(core.cpu, &set);
	if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0)
	{
		g_OriginalAffinity = original;
		g_Pinned = true;
	}
}

bool RestoreCurrentThreadAffinity()
{
	if (!g_Pinned || pthread_setaffinity_np(
		pthread_self(), sizeof(g_OriginalAffinity), &g_OriginalAffinity) != 0)
	{
		return false;
	}
	g_Pinned = false;
	return true;
}

void KeepCurrentThreadOffRenderCore()
{
	const RenderCore& core = GetRenderCore();
	if (core.cpu < 0)
		return;
	cpu_set_t set;
	if (pthread_getaffinity_np(pthread_self(), sizeof(set), &set) != 0)
		return;
	for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
		if (CPU_ISSET(cpu, &core.siblings))
			CPU_CLR(cpu, &set);
	// Workers share data with the render thread, so prefer its cache domain.
	if (CPU_COUNT(&core.neighbours) >= 6)
	{
		cpu_set_t near;
		CPU_AND(&near, &set, &core.neighbours);
		if (CPU_COUNT(&near) >= 6)
			set = near;
	}
	if (CPU_COUNT(&set) > 0)
		pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

void KeepCurrentThreadOffRenderCacheDomain()
{
	const RenderCore& core = GetRenderCore();
	if (core.cpu < 0)
		return;
	cpu_set_t set;
	if (pthread_getaffinity_np(pthread_self(), sizeof(set), &set) != 0)
		return;
	for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
		if (CPU_ISSET(cpu, &core.neighbours) || CPU_ISSET(cpu, &core.siblings))
			CPU_CLR(cpu, &set);
	if (CPU_COUNT(&set) < 4)
	{
		KeepCurrentThreadOffRenderCore();
		return;
	}
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

#else

void PinCurrentThreadToRenderCpu() {}
void KeepCurrentThreadOffRenderCacheDomain() {}
bool RestoreCurrentThreadAffinity() { return false; }
void KeepCurrentThreadOffRenderCore() {}

#endif

ScopedUnpin::ScopedUnpin() : m_Repin{RestoreCurrentThreadAffinity()}
{
}

ScopedUnpin::~ScopedUnpin()
{
	if (m_Repin)
		PinCurrentThreadToRenderCpu();
}

} // namespace ThreadAffinity
