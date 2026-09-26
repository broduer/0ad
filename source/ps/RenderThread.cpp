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
#include "ps/Profiler2.h"
#include "ps/ThreadAffinity.h"
#include "ps/Threading.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
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

	std::atomic<int> pauseRequests{0};
	bool parked = false;

	// Frame and step pairing: frames signalled, the step the logic thread is
	// on, steps finished and frames that have taken their step.
	bool lockstep = false;
	uint64_t signals = 0;
	uint64_t stepIndex = 0;
	uint64_t stepsDone = 0;
	uint64_t framesTaken = 0;
	// Whether the frame last signalled shows the step it starts rather than
	// the one before, and whether the logic thread's step is shown so.
	bool showCurrentStep = false;
	bool stepShownCurrent = false;
};

State& GetState()
{
	static State state;
	return state;
}

std::atomic<int> g_Threaded{-1};

std::function<void()> g_BindDevice = [] {};
std::function<void()> g_UnbindDevice = [] {};

} // anonymous namespace

void SetDeviceHandoff(std::function<void()> bind, std::function<void()> unbind)
{
	g_BindDevice = std::move(bind);
	g_UnbindDevice = std::move(unbind);
}

bool IsThreaded()
{
	int threaded = g_Threaded.load(std::memory_order_relaxed);
	if (threaded < 0)
	{
		// Non-visual runs have no config.
		if (!CConfigDB::IsInitialised())
			return false;
		threaded = g_ConfigDB.Get("renderthread", true) ? 1 : 0;
		g_Threaded.store(threaded, std::memory_order_relaxed);
	}
	return threaded == 1;
}

bool IsRenderThread()
{
	return g_IsRenderThread || !GetState().running;
}

bool IsDedicatedRenderThread()
{
	return g_IsRenderThread;
}

void Start(std::function<void()> frame)
{
	State& state = GetState();
	ENSURE(!state.running);
	state.running = true;
	state.stop = false;
	g_UnbindDevice();
	state.thread = Threading::CreateThread([frame = std::move(frame), &state]()
	{
		g_IsRenderThread = true;
		debug_SetThreadName("render");
		g_Profiler2.RegisterCurrentThread("render");
		if (g_ConfigDB.Get("renderthread.pin", true))
			ThreadAffinity::PinCurrentThreadToRenderCpu();
		bool bound = false;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> lock(state.mutex);
				while (!state.stop && state.pauseRequests > 0)
				{
					if (bound)
					{
						g_UnbindDevice();
						bound = false;
					}
					state.parked = true;
					state.condition.notify_all();
					state.condition.wait(lock, [&state] { return state.stop || state.pauseRequests == 0; });
					state.parked = false;
				}
				if (state.stop)
				{
					if (bound)
						g_UnbindDevice();
					return;
				}
				if (!bound)
				{
					g_BindDevice();
					bound = true;
				}
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
	g_BindDevice();
}

void SignalFrame(const double realTime, const bool showCurrentStep)
{
	State& state = GetState();
	if (!state.running)
	{
		state.pendingRealTime += realTime;
		state.framePending = true;
		return;
	}
	std::lock_guard<std::mutex> lock(state.mutex);
	state.showCurrentStep = showCurrentStep;
	// Paired with a frame, a step advances by the length of that frame, as
	// without the thread; frames rendered while the logic thread is paused or
	// busy (loading, starting the game) aren't game time.
	if (state.lockstep || showCurrentStep)
		state.pendingRealTime = realTime;
	else
		state.pendingRealTime += realTime;
	state.framePending = true;
	++state.signals;
	state.condition.notify_all();
}

void SetLockstep(const bool lockstep)
{
	GetState().lockstep = lockstep;
}

void WaitForLogicStep()
{
	State& state = GetState();
	if (!state.running || !g_IsRenderThread)
		return;
	std::unique_lock<std::mutex> lock(state.mutex);
	if (!state.lockstep && !state.showCurrentStep)
		return;
	// Frame n shows step n, or, overlapping it, step n - 1. A pause needs the
	// frame to end, so it does not wait.
	const uint64_t step{state.showCurrentStep ? state.signals : state.signals - 1};
	state.condition.wait(lock, [&state, step] {
		return state.stop || state.pauseRequests > 0 || state.stepsDone >= step;
	});
}

void MarkFrameTaken()
{
	State& state = GetState();
	if (!state.running || !g_IsRenderThread)
		return;
	std::lock_guard<std::mutex> lock(state.mutex);
	state.framesTaken = state.signals;
	state.condition.notify_all();
}

void WaitForFrameTaken()
{
	State& state = GetState();
	if (!state.running || g_IsRenderThread)
		return;
	std::unique_lock<std::mutex> lock(state.mutex);
	if (!state.lockstep && !state.stepShownCurrent)
		return;
	// Step n publishes once the frame before the one showing it has taken its
	// step. A parked render thread takes nothing.
	const uint64_t frame{state.stepShownCurrent ? state.stepIndex - 1 : state.stepIndex};
	state.condition.wait(lock, [&state, frame] {
		return state.stop || state.pauseRequests > 0 || state.framesTaken >= frame;
	});
}

void MarkLogicStepDone()
{
	State& state = GetState();
	if (!state.running || g_IsRenderThread)
		return;
	std::lock_guard<std::mutex> lock(state.mutex);
	state.stepsDone = state.stepIndex;
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
	state.stepIndex = state.signals;
	state.stepShownCurrent = state.showCurrentStep;
	return std::exchange(state.pendingRealTime, 0.0);
}

Pause::Pause() : m_Active(GetState().running && !g_IsRenderThread)
{
	if (!m_Active)
		return;
	State& state = GetState();
	std::unique_lock<std::mutex> lock(state.mutex);
	const bool outermost = ++state.pauseRequests == 1;
	// A frame waiting for the logic step ends instead, so it can park.
	state.condition.notify_all();
	state.condition.wait(lock, [&state] { return state.parked || state.stop; });
	if (outermost && !state.stop)
		g_BindDevice();
}

Pause::~Pause()
{
	if (!m_Active)
		return;
	State& state = GetState();
	std::lock_guard<std::mutex> lock(state.mutex);
	if (state.pauseRequests == 1 && !state.stop)
		g_UnbindDevice();
	--state.pauseRequests;
	state.condition.notify_all();
}

} // namespace RenderThread
