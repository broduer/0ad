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

#include "renderer/backend/FramePacer.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

namespace
{

using Renderer::Backend::CFramePacer;

constexpr uint64_t MS{1'000'000};
constexpr uint64_t PERIOD{6'060'606};

struct SSetup
{
	uint64_t cpu{1 * MS};
	uint64_t gpu{2 * MS};
	uint64_t jitter{MS / 2};
	// Every turnEvery-th frame shows a simulation turn, which takes turnCpu
	// more CPU time.
	uint64_t turnEvery{0};
	uint64_t turnCpu{0};
	// Of every heavyPer frames, the first heavyOf take heavyCpu of CPU time
	// instead of cpu.
	uint64_t heavyCpu{0};
	uint32_t heavyOf{0};
	uint32_t heavyPer{10};
	// hitchCount frames from hitchStart, hitchEvery apart, take hitch more CPU
	// time, as a game's first frames do.
	uint64_t hitch{0};
	size_t hitchStart{0};
	uint32_t hitchCount{1};
	uint32_t hitchEvery{1};
	// Frames that each take so much more CPU time.
	std::vector<std::pair<size_t, uint64_t>> spikes;
	// Every lateWakeEvery frames the thread wakes lateWake after the frame's
	// start, as on a CPU busy with other threads.
	uint64_t lateWake{0};
	uint32_t lateWakeEvery{0};
	// How long before a vblank the display needs a frame finished.
	uint64_t displayLatch{1 * MS};
	// How far off the clock GPU completion is reported on.
	uint64_t readyError{0};
	// Whether the display honours target times and reports exact presentation
	// and GPU completion times, or only presents in order and is timed by
	// waiting for it.
	bool presentTiming{true};
	// How long after a frame is shown its timing report arrives, and whether
	// present wait is asked about frames overdue meanwhile.
	uint64_t reportDelay{0};
	bool checkOverdue{true};
	// Every hiccupEvery frames the display shows a frame a refresh later than
	// it could, as a compositor that misses its own deadline does.
	uint64_t hiccupEvery{0};
	// Whether the display shows a frame displayLatch after it's asked for and
	// ready, a refresh after the previous one at the earliest, until frame
	// fixedFrom; and what the pacer is told of its refresh.
	bool variableRefresh{false};
	size_t fixedFrom{SIZE_MAX};
	CFramePacer::RefreshMode mode{CFramePacer::RefreshMode::FIXED};
};

struct SResult
{
	double refreshesPerFrame{0.0};
	uint64_t medianLatency{0};
	// The 95th percentile of start-to-shown times less the 5th.
	uint64_t latencySpread{0};
	uint64_t misses{0};
	// Frames shown after the vblank they were scheduled for.
	uint64_t lateFrames{0};
	// Steps in whole refreshes, rounded.
	uint32_t minInterval{0};
	uint32_t maxInterval{0};
	uint32_t finalInterval{0};
	uint64_t finalStep{0};
	bool variable{false};
	uint64_t period{0};
};

/**
 * Run frames through the pacer against a simulated renderer and display, and
 * measure the frames after the first @p warmup.
 */
SResult Simulate(const SSetup& setup, const size_t frames = 4000, const size_t warmup = 1000)
{
	CFramePacer pacer;
	if (setup.presentTiming)
		pacer.SetRefreshPeriod(PERIOD, setup.mode);
	pacer.SetEveryPresentationReported(setup.presentTiming);

	uint32_t random{12345};
	const auto jitter = [&random, &setup]() -> uint64_t
	{
		random = random * 1664525u + 1013904223u;
		return (random >> 8) % (setup.jitter + 1);
	};
	const uint64_t phase{123'456};
	const auto vblankFrom = [phase](const uint64_t time)
	{
		return time <= phase ? phase : phase + (time - phase + PERIOD - 1) / PERIOD * PERIOD;
	};

	struct SShown
	{
		uint64_t id;
		uint64_t ready;
		uint64_t shown;
	};
	std::deque<SShown> feedback;
	// The latest frame known to be shown, from its report or present wait.
	uint64_t shownID{0};
	// Report presentations up to @p now, exactly or with unknown times.
	const auto deliver = [&feedback, &pacer, &setup, &shownID](const uint64_t now, const bool exact)
	{
		for (; !feedback.empty() && feedback.front().shown + (exact ? setup.reportDelay : 0) <= now;
			feedback.pop_front())
		{
			shownID = std::max(shownID, feedback.front().id);
			if (setup.presentTiming)
				pacer.OnFrameReady(feedback.front().id, feedback.front().ready + setup.readyError);
			pacer.OnFrameDisplayed(feedback.front().id, exact ? feedback.front().shown : 0);
		}
	};

	std::vector<uint64_t> shown(frames + 1);
	std::vector<uint64_t> latencies;
	uint64_t cpuFree{1000 * MS};
	uint64_t gpuEnd{0};
	uint64_t lastShown{0};
	uint64_t missesBefore{0};
	SResult result;
	result.minInterval = ~0u;
	for (size_t id = 1; id <= frames; ++id)
	{
		// Feedback is read as the frame starts. Without present timing, what
		// was shown while the previous frame worked has no known time.
		deliver(cpuFree, setup.presentTiming);
		// As the swapchain does, ask present wait about the oldest frame not
		// known to be shown once it's overdue: report it if it isn't shown, and
		// return whether it wasn't.
		const auto nextOverdue = [&pacer, &shownID, id]()
		{
			return shownID + 1 < id ? pacer.GetOverdueTime(shownID + 1) : 0;
		};
		const auto checkOverdue = [&](const uint64_t now)
		{
			if (!setup.presentTiming || !setup.checkOverdue)
				return false;
			for (uint64_t overdue{nextOverdue()}; overdue != 0 && now > overdue; overdue = nextOverdue())
			{
				if (shown[shownID + 1] > now)
				{
					pacer.OnFrameNotShownBy(shownID + 1, now);
					return true;
				}
				++shownID;
			}
			return false;
		};
		checkOverdue(cpuFree);
		const bool isTurn{setup.turnEvery != 0 && id % setup.turnEvery == 0};
		CFramePacer::SSchedule schedule{pacer.Schedule(cpuFree)};
		uint64_t start{cpuFree};
		if (schedule.deadline != 0)
		{
			start = std::max(cpuFree, schedule.start);
			if (setup.presentTiming && setup.checkOverdue)
			{
				for (uint64_t overdue{nextOverdue()}; overdue != 0 && overdue < start; overdue = nextOverdue())
				{
					if (checkOverdue(overdue + 1))
					{
						schedule = pacer.Schedule(overdue + 1);
						start = std::max(overdue + 1, schedule.start);
					}
				}
			}
		}
		else if (id > 2)
			start = std::max(cpuFree, shown[id - 2]);
		// Waiting for presents times those shown meanwhile exactly, and reports
		// the one still pending as not shown by the start.
		if (!setup.presentTiming && start > cpuFree)
		{
			deliver(start, true);
			if (!feedback.empty())
				pacer.OnFrameNotShownBy(feedback.front().id, start);
		}
		if (setup.lateWakeEvery != 0 && id % setup.lateWakeEvery == 0)
			start += setup.lateWake;
		pacer.OnFrameStarted(id, start, schedule);

		const bool hitch{setup.hitch != 0 && id >= setup.hitchStart &&
			(id - setup.hitchStart) % setup.hitchEvery == 0 &&
			(id - setup.hitchStart) / setup.hitchEvery < setup.hitchCount};
		uint64_t extra{(isTurn ? setup.turnCpu : 0) + (hitch ? setup.hitch : 0)};
		for (const auto& [frame, spike] : setup.spikes)
			if (frame == id)
				extra += spike;
		const bool heavy{setup.heavyOf != 0 && id % setup.heavyPer < setup.heavyOf};
		const uint64_t presented{start + (heavy ? setup.heavyCpu : setup.cpu) + jitter() + extra};
		pacer.OnFramePresented(id, presented);
		cpuFree = presented;
		gpuEnd = std::max(presented, gpuEnd) + setup.gpu + jitter();

		uint64_t asked{0};
		if (setup.presentTiming && schedule.deadline != 0)
			asked = schedule.nearestRefresh ? schedule.target - PERIOD / 2 : schedule.target;
		const bool hiccup{setup.hiccupEvery != 0 && id % setup.hiccupEvery == 0};
		const bool variable{setup.variableRefresh && id < setup.fixedFrom};
		const uint64_t earliest{variable ? std::max(gpuEnd, asked) + setup.displayLatch :
			vblankFrom(std::max(gpuEnd + setup.displayLatch, asked))};
		lastShown = std::max(earliest + (hiccup ? PERIOD : 0), lastShown + PERIOD);
		shown[id] = lastShown;
		feedback.push_back({id, gpuEnd, lastShown});

		if (id == warmup)
			missesBefore = pacer.GetMisses();
		if (id > warmup)
		{
			latencies.push_back(shown[id] - start);
			if (schedule.deadline != 0 && shown[id] > schedule.deadline + (variable ? PERIOD / 4 : PERIOD / 2))
			{
				++result.lateFrames;
			}
			const uint32_t interval{static_cast<uint32_t>((pacer.GetStep() + PERIOD / 2) / PERIOD)};
			result.minInterval = std::min(result.minInterval, interval);
			result.maxInterval = std::max(result.maxInterval, interval);
		}
	}
	result.refreshesPerFrame = static_cast<double>(shown[frames] - shown[warmup]) /
		static_cast<double>(PERIOD * (frames - warmup));
	std::sort(latencies.begin(), latencies.end());
	result.medianLatency = latencies[latencies.size() / 2];
	result.latencySpread = latencies[latencies.size() * 95 / 100] - latencies[latencies.size() * 5 / 100];
	result.misses = pacer.GetMisses() - missesBefore;
	result.finalStep = pacer.GetStep();
	result.finalInterval = static_cast<uint32_t>((result.finalStep + PERIOD / 2) / PERIOD);
	result.variable = pacer.IsRefreshVariable();
	result.period = pacer.GetRefreshPeriod();
	return result;
}

} // anonymous namespace

class TestFramePacer : public CxxTest::TestSuite
{
public:
	void test_present_timing()
	{
		const SResult result{Simulate({})};
		// One frame every refresh, each shown within a refresh of its start
		// rather than the two a queued frame takes.
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
		TS_ASSERT_LESS_THAN(result.medianLatency, PERIOD);
		TS_ASSERT_LESS_THAN(result.misses, 30u);
		TS_ASSERT_EQUALS(result.maxInterval, 1u);
	}

	void test_present_wait_only()
	{
		SSetup setup;
		setup.presentTiming = false;
		const SResult result{Simulate(setup)};
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
		TS_ASSERT_LESS_THAN(result.medianLatency, PERIOD);
		TS_ASSERT_LESS_THAN(result.misses, 30u);
		// The period is estimated from the presentations alone.
		TS_ASSERT_LESS_THAN(std::max(result.period, PERIOD) - std::min(result.period, PERIOD), PERIOD / 200);
	}

	void test_present_wait_only_slow_gpu()
	{
		// Frames that take longer than a refresh to finish on the GPU are
		// shown after the next one starts, so waiting doesn't see them on
		// screen. They mustn't end up later than a queued frame would.
		SSetup setup;
		setup.cpu = 3 * MS;
		setup.gpu = 5 * MS;
		setup.jitter = MS / 4;
		setup.presentTiming = false;
		const SResult result{Simulate(setup)};
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
		TS_ASSERT_LESS_THAN_EQUALS(result.medianLatency, 2 * PERIOD + MS / 2);
	}

	void test_overlapping_frames()
	{
		// Each frame takes longer than a refresh from start to finish, but
		// its CPU and GPU work each fit in one, so the frame rate holds.
		SSetup setup;
		setup.cpu = 4 * MS;
		setup.gpu = 5 * MS;
		setup.jitter = MS / 4;
		for (const bool presentTiming : {true, false})
		{
			setup.presentTiming = presentTiming;
			const SResult result{Simulate(setup)};
			TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
			TS_ASSERT_EQUALS(result.maxInterval, 1u);
		}
	}

	void test_slow_frames()
	{
		// A frame that doesn't fit in a refresh settles on every other one,
		// and stays there.
		SSetup setup;
		setup.cpu = 8 * MS;
		const SResult result{Simulate(setup)};
		TS_ASSERT_DELTA(result.refreshesPerFrame, 2.0, 0.02);
		TS_ASSERT_EQUALS(result.minInterval, 2u);
		TS_ASSERT_EQUALS(result.maxInterval, 2u);

		// Waiting on presentation never catches such frames on screen, so
		// they go unpaced, as fast as the display takes them.
		setup.presentTiming = false;
		TS_ASSERT_DELTA(Simulate(setup).refreshesPerFrame, 8.25 * MS / PERIOD, 0.05);
	}

	void test_turn_frames()
	{
		// Turn frames neither drop the frame rate nor miss, nor make latency
		// vary.
		SSetup setup;
		setup.turnEvery = 33;
		setup.turnCpu = 2 * MS;
		for (const bool presentTiming : {true, false})
		{
			setup.presentTiming = presentTiming;
			const SResult result{Simulate(setup)};
			TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
			TS_ASSERT_LESS_THAN(result.misses, 30u);
			if (presentTiming)
				TS_ASSERT_LESS_THAN(result.latencySpread, MS);
		}
	}

	void test_long_turn_frames()
	{
		// Turn frames take longer than a refresh. Every frame starts early
		// enough for one, so input is read the same time before each vblank,
		// and the refresh rate isn't divided.
		SSetup setup;
		setup.turnEvery = 33;
		setup.turnCpu = 8 * MS;
		const SResult result{Simulate(setup)};
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
		TS_ASSERT_LESS_THAN(result.latencySpread, MS);
		TS_ASSERT_LESS_THAN(result.misses, 30u);
		TS_ASSERT_EQUALS(result.maxInterval, 1u);

		// Waiting on presentation can't start frames more than a refresh
		// early, so they go unpaced but keep the frame rate.
		setup.presentTiming = false;
		TS_ASSERT_LESS_THAN(Simulate(setup).refreshesPerFrame, 1.05);
	}

	void test_unfitting_turn_frames()
	{
		// Turn frames take longer than the deepest lead at every refresh.
		// Turns recur, so that's the load, not a one-off: every other refresh,
		// with the turn frames on time.
		SSetup setup;
		setup.turnEvery = 33;
		setup.turnCpu = 20 * MS;
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.minInterval, 2u);
		TS_ASSERT_LESS_THAN(result.lateFrames, 5u);
		TS_ASSERT_DELTA(result.refreshesPerFrame, 2.0, 0.02);
	}

	void test_late_wakes()
	{
		// The thread now and then wakes 2 ms late for a frame. That's part of
		// a frame's cost, so frames start early enough for it.
		SSetup setup;
		setup.lateWake = 2 * MS;
		setup.lateWakeEvery = 10;
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.maxInterval, 1u);
		TS_ASSERT_LESS_THAN(result.lateFrames, 5u);
		TS_ASSERT_LESS_THAN(result.latencySpread, 3 * MS);
	}

	void test_absorbable_overruns()
	{
		// Three frames in ten take longer than a refresh on the CPU, but the
		// others have the slack to catch up, so every refresh holds.
		SSetup setup;
		setup.heavyCpu = 7 * MS;
		setup.heavyOf = 3;
		setup.heavyPer = 10;
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.maxInterval, 1u);
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
		TS_ASSERT_LESS_THAN(result.misses, 30u);
	}

	void test_unabsorbable_overruns()
	{
		// Two frames in ten take two refreshes on the CPU and the others nearly
		// one, which can't catch up: every other refresh, and it stays there.
		SSetup setup;
		setup.cpu = 11 * MS / 2;
		setup.heavyCpu = 12 * MS;
		setup.heavyOf = 2;
		setup.heavyPer = 10;
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.minInterval, 2u);
		TS_ASSERT_EQUALS(result.maxInterval, 2u);
		TS_ASSERT_DELTA(result.refreshesPerFrame, 2.0, 0.02);
		TS_ASSERT_LESS_THAN(result.misses, 30u);
	}

	void test_one_off_turn()
	{
		// A game's first turn takes far longer than any lead covers; it
		// mustn't hold later frames at a deeper lead.
		SSetup setup;
		setup.turnEvery = 33;
		setup.turnCpu = 2 * MS;
		const SResult steady{Simulate(setup)};
		// The first turn frame after the warmup.
		setup.hitchStart = 1023;
		setup.hitch = 200 * MS;
		const SResult result{Simulate(setup)};
		TS_ASSERT_LESS_THAN(result.medianLatency, steady.medianLatency + MS);
		TS_ASSERT_LESS_THAN(result.latencySpread, MS);
	}

	void test_startup_hitches()
	{
		// A game's first frames take several times longer than any lead
		// covers. They're one-offs, so the frame rate holds.
		SSetup setup;
		setup.hitchStart = 1005;
		setup.hitch = 30 * MS;
		setup.hitchCount = 5;
		setup.hitchEvery = 7;
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.maxInterval, 1u);
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.01);
	}

	void test_first_frames()
	{
		// A game's first frames come far apart, and one takes far longer than
		// the deepest lead. That one misses however far apart frames are, so
		// every refresh holds once pacing starts.
		CFramePacer pacer;
		pacer.SetRefreshPeriod(PERIOD, CFramePacer::RefreshMode::FIXED);
		uint64_t time{1000 * MS};
		for (uint64_t id = 1; id <= 41; ++id)
		{
			pacer.OnFrameStarted(id, time, {});
			const uint64_t presented{time + (id == 20 ? 155 * MS : 3 * MS)};
			pacer.OnFramePresented(id, presented);
			pacer.OnFrameReady(id, presented + 2 * MS);
			const uint64_t shown{(presented + 3 * MS) / PERIOD * PERIOD + PERIOD};
			pacer.OnFrameDisplayed(id, shown);
			time = shown + 18 * MS;
		}
		pacer.Schedule(time);
		TS_ASSERT_EQUALS(pacer.GetStep(), PERIOD);
	}

	void test_bursts()
	{
		// A burst of slow frames, as when new textures load, misses at any
		// interval. Every refresh holds through a short one, and comes back
		// soon after a long one.
		SSetup setup;
		setup.turnEvery = 33;
		setup.turnCpu = 2 * MS;
		setup.hitchStart = 1500;
		setup.hitch = 9 * MS;
		setup.hitchCount = 20;
		const SResult brief{Simulate(setup)};
		TS_ASSERT_EQUALS(brief.maxInterval, 1u);
		TS_ASSERT_LESS_THAN(brief.refreshesPerFrame, 1.02);

		setup.hitchCount = 300;
		const SResult sustained{Simulate(setup)};
		TS_ASSERT_LESS_THAN_EQUALS(2u, sustained.maxInterval);
		TS_ASSERT_EQUALS(sustained.finalInterval, 1u);
		TS_ASSERT_LESS_THAN(sustained.refreshesPerFrame, 1.25);
	}

	void test_isolated_slow_frames()
	{
		// A few frames in as many seconds take longer than the budget, but
		// only one longer than the deepest lead at every refresh. They miss
		// however far apart frames are shown, so every refresh holds.
		SSetup setup;
		setup.turnEvery = 33;
		setup.turnCpu = MS / 2;
		setup.spikes = {{1500, 6 * MS}, {1565, 32 * MS}, {1956, 12 * MS}};
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.maxInterval, 1u);
	}

	void test_sustained_hitches()
	{
		// Every frame takes longer than the deepest lead at every refresh:
		// that's the load, not hitches.
		SSetup setup;
		setup.cpu = 20 * MS;
		const SResult result{Simulate(setup)};
		TS_ASSERT_LESS_THAN_EQUALS(result.minInterval, 4u);
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 4.05);
		TS_ASSERT_LESS_THAN(result.misses, 30u);
	}

	void test_display_hiccups()
	{
		// A compositor that misses a vblank delays the frames queued behind it.
		// Present wait tells of the miss before the timing report does, so
		// fewer frames aim at vblanks already taken.
		SSetup setup;
		setup.reportDelay = 2 * PERIOD;
		setup.hiccupEvery = 300;
		const uint64_t hiccups{3000 / 300};
		setup.checkOverdue = false;
		const SResult unchecked{Simulate(setup)};
		setup.checkOverdue = true;
		const SResult checked{Simulate(setup)};
		TS_ASSERT_LESS_THAN(checked.lateFrames, unchecked.lateFrames);
		TS_ASSERT_LESS_THAN_EQUALS(checked.lateFrames, 2 * hiccups);
		TS_ASSERT_LESS_THAN(checked.refreshesPerFrame, 1.01);

		setup.variableRefresh = true;
		setup.mode = CFramePacer::RefreshMode::VARIABLE;
		const SResult variable{Simulate(setup)};
		TS_ASSERT_LESS_THAN_EQUALS(variable.lateFrames, 2 * hiccups);
		TS_ASSERT_LESS_THAN(variable.refreshesPerFrame, 1.01);
	}

	void test_variable_refresh()
	{
		// Frames that don't fit in a refresh are shown as far apart as they
		// need, not every other refresh, still at a consistent latency.
		SSetup setup;
		setup.cpu = 7 * MS;
		setup.variableRefresh = true;
		setup.mode = CFramePacer::RefreshMode::VARIABLE;
		const SResult result{Simulate(setup)};
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.4);
		TS_ASSERT_LESS_THAN(result.latencySpread, MS);
		TS_ASSERT_LESS_THAN(result.lateFrames, 5u);

		// So are frames far longer than a refresh.
		setup.cpu = 20 * MS;
		const SResult slow{Simulate(setup)};
		TS_ASSERT_LESS_THAN(slow.refreshesPerFrame, 3.6);
		TS_ASSERT_LESS_THAN(slow.latencySpread, MS);
		TS_ASSERT_LESS_THAN(slow.lateFrames, 5u);

		// Frames that fit are paced as for a fixed refresh.
		setup.cpu = 1 * MS;
		setup.turnEvery = 33;
		setup.turnCpu = 2 * MS;
		const SResult turns{Simulate(setup)};
		TS_ASSERT_LESS_THAN(turns.refreshesPerFrame, 1.01);
		TS_ASSERT_LESS_THAN(turns.latencySpread, MS);
		TS_ASSERT_LESS_THAN(turns.misses, 30u);
	}

	void test_variable_refresh_delay()
	{
		// The display shows frames longer after asked than a miss is late by.
		// Frames ask that much earlier, and aren't judged late.
		SSetup setup;
		setup.cpu = 7 * MS;
		setup.displayLatch = 2 * MS;
		setup.variableRefresh = true;
		setup.mode = CFramePacer::RefreshMode::UNKNOWN;
		const SResult result{Simulate(setup)};
		TS_ASSERT(result.variable);
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.4);
		TS_ASSERT_LESS_THAN(result.latencySpread, MS);
		TS_ASSERT_LESS_THAN(result.lateFrames, 5u);
	}

	void test_refresh_detection()
	{
		// Where the refresh isn't known, frames that don't fit in one tell
		// which it is.
		SSetup setup;
		setup.cpu = 7 * MS;
		setup.mode = CFramePacer::RefreshMode::UNKNOWN;
		const SResult fixed{Simulate(setup)};
		TS_ASSERT(!fixed.variable);
		TS_ASSERT_DELTA(fixed.refreshesPerFrame, 2.0, 0.02);
		TS_ASSERT_LESS_THAN(fixed.lateFrames, 5u);

		setup.variableRefresh = true;
		const SResult variable{Simulate(setup)};
		TS_ASSERT(variable.variable);
		TS_ASSERT_LESS_THAN(variable.refreshesPerFrame, 1.4);
		TS_ASSERT_LESS_THAN(variable.lateFrames, 5u);

		// A display that turns its variable refresh off is paced for a fixed
		// one from then on.
		setup.fixedFrom = 2000;
		const SResult stopped{Simulate(setup)};
		TS_ASSERT(!stopped.variable);
		TS_ASSERT_EQUALS(stopped.finalInterval, 2u);
		TS_ASSERT_LESS_THAN(stopped.lateFrames, 10u);
	}

	void test_miscalibrated_ready()
	{
		// GPU completion is reported on a clock far ahead, after the frames are
		// shown. The frame rate recovers after startup hitches regardless.
		SSetup setup;
		setup.readyError = 270 * MS;
		setup.hitchStart = 1005;
		setup.hitch = 30 * MS;
		setup.hitchCount = 5;
		setup.hitchEvery = 7;
		const SResult result{Simulate(setup)};
		TS_ASSERT_EQUALS(result.finalInterval, 1u);
		TS_ASSERT_LESS_THAN(result.refreshesPerFrame, 1.05);
	}

	void test_unknown_refresh()
	{
		// A display without a fixed refresh gets no schedule.
		CFramePacer pacer;
		pacer.SetRefreshPeriod(0, CFramePacer::RefreshMode::UNKNOWN);
		for (uint64_t id = 1; id < 100; ++id)
		{
			pacer.OnFrameStarted(id, id * 10 * MS, {});
			pacer.OnFramePresented(id, id * 10 * MS + MS);
			pacer.OnFrameDisplayed(id, id * 10 * MS + 5 * MS);
		}
		TS_ASSERT_EQUALS(pacer.Schedule(1000 * MS).deadline, 0u);
	}
};
