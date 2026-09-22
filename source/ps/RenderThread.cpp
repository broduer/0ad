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

#include "RenderThread.h"

#include "lib/debug.h"
#include "ps/ConfigDB.h"
#include "ps/Threading.h"

#include <atomic>
#include <condition_variable>
#include <thread>
#include <utility>

namespace RenderThread
{

namespace
{

thread_local bool g_IsRenderThread = false;

struct State
{
	std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;
	bool running = false;
	bool stop = false;

	// Elapsed time accumulated since the last logic step.
	double pendingRealTime = 0.0;
	bool framePending = false;

	int pauseRequests = 0;
	bool parked = false;
};

State& GetState()
{
	static State state;
	return state;
}

std::atomic<int> g_Threaded{-1};

} // anonymous namespace

bool IsThreaded()
{
	int threaded = g_Threaded.load(std::memory_order_relaxed);
	if (threaded < 0)
	{
		threaded = g_ConfigDB.Get("renderthread", false) ? 1 : 0;
		g_Threaded.store(threaded, std::memory_order_relaxed);
	}
	return threaded == 1;
}

bool IsRenderThread()
{
	return g_IsRenderThread || !GetState().running;
}

void Start(std::function<void()> frame)
{
	State& state = GetState();
	ENSURE(!state.running);
	state.running = true;
	state.stop = false;
	state.thread = Threading::CreateThread([frame = std::move(frame), &state]()
	{
		g_IsRenderThread = true;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> lock(state.mutex);
				state.parked = true;
				state.condition.notify_all();
				state.condition.wait(lock, [&state] { return state.stop || state.pauseRequests == 0; });
				state.parked = false;
				if (state.stop)
					return;
			}
			frame();
		}
	});
}

void Stop()
{
	State& state = GetState();
	if (!state.running)
		return;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		state.stop = true;
		state.condition.notify_all();
	}
	state.thread.join();
	state.running = false;
}

void SignalFrame(const double realTime)
{
	State& state = GetState();
	if (!state.running)
	{
		state.pendingRealTime += realTime;
		state.framePending = true;
		return;
	}
	std::lock_guard<std::mutex> lock(state.mutex);
	state.pendingRealTime += realTime;
	state.framePending = true;
	state.condition.notify_all();
}

double WaitForFrames()
{
	State& state = GetState();
	if (!state.running)
	{
		state.framePending = false;
		return std::exchange(state.pendingRealTime, 0.0);
	}
	std::unique_lock<std::mutex> lock(state.mutex);
	state.condition.wait(lock, [&state] { return state.framePending || state.stop; });
	state.framePending = false;
	return std::exchange(state.pendingRealTime, 0.0);
}

Pause::Pause()
{
	State& state = GetState();
	if (!state.running)
		return;
	std::unique_lock<std::mutex> lock(state.mutex);
	++state.pauseRequests;
	state.condition.wait(lock, [&state] { return state.parked || state.stop; });
}

Pause::~Pause()
{
	State& state = GetState();
	if (!state.running)
		return;
	std::lock_guard<std::mutex> lock(state.mutex);
	--state.pauseRequests;
	state.condition.notify_all();
}

} // namespace RenderThread
