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

#ifndef INCLUDED_RENDERER_BACKEND_FRAMEPACER
#define INCLUDED_RENDERER_BACKEND_FRAMEPACER

#include <array>
#include <cstddef>
#include <cstdint>

namespace Renderer
{

namespace Backend
{

/**
 * Schedules frames just in time for the display. Every frame starts the same
 * time, the budget, before the vblank it aims at, so it reads input at a
 * consistent point relative to the display. Times are nanoseconds on the clock
 * the display reports presentation times in.
 *
 * The budget and the step, how far apart frames are shown, come from replaying
 * the recent frames' CPU and GPU times through a queue: a frame's CPU work
 * starts when asked or once the previous frame's is done, and its GPU work once
 * it's submitted and the previous frame's is done. A frame's lead, from its
 * asked-for start to ready, includes any delay the frames before it pass on.
 * The budget is the largest lead of the last few seconds plus the latch
 * requirement, so frames with a simulation turn are covered like any other.
 *
 * The step is the shortest that covers the frames' average cost and at which
 * the frames since it last changed fit the deepest lead the display queue
 * allows, but for a couple of episodes of misses. Misses close together are one
 * episode, so a burst of slow frames, which misses at any step, doesn't lower
 * the frame rate, while misses that recur, as every turn's would, do. Frames
 * that actually missed taking longer than the deepest lead count too, since
 * the replay is only a model; frames that missed only for a budget that hadn't
 * yet seen one so long don't, since the budget grows to cover them, nor do
 * frames the display showed late though they were ready. A
 * shorter step is tried once the latest frames fit it with no misses and a
 * step to spare, and less often each time it fails. Every span is a time
 * rather than a number of frames, so the rules don't change with the frame
 * rate.
 *
 * The latch requirement, how far ahead of a vblank the display needs a frame,
 * rises a fixed amount when two frames miss close together and decays after a
 * long clean run. The amount is fixed because a missed frame's lead is the
 * budget chosen here; feeding it back would chase its own output up to a
 * whole refresh of lead, which is plain FIFO.
 *
 * Deadlines sit on a grid extrapolated by the refresh period from the last
 * presentation, so they drift from the vblanks by a fraction of a refresh.
 * Snapping to the grid tolerates a quarter refresh; rounding strictly up would
 * skip a vblank whenever a grid point lands just past the earliest feasible
 * time.
 *
 * A variable refresh shows a frame when asked, at least a refresh after the
 * previous one, so the step is any multiple of a sixteenth of a refresh and
 * deadlines aren't snapped. Frames ask to be shown the display's measured
 * delay before their deadline. Where the display doesn't say which refresh it
 * has, frames shown between vblanks mean it's variable, and frames ready in
 * time but shown late mean it's fixed.
 */
class CFramePacer
{
public:
	enum class RefreshMode
	{
		UNKNOWN,
		FIXED,
		VARIABLE
	};

	struct SSchedule
	{
		// When the frame should start and the vblank it aims at, both 0 when
		// there's nothing to lock to yet.
		uint64_t start{0};
		uint64_t deadline{0};
		// When to ask the display to show the frame, and whether it should
		// round that to the nearest refresh.
		uint64_t target{0};
		bool nearestRefresh{false};
	};

	/**
	 * Set the refresh period, the shortest for a variable refresh, or 0 for
	 * none. Without it the period is estimated from presentation times.
	 */
	void SetRefreshPeriod(const uint64_t period, const RefreshMode mode);

	/**
	 * Set whether every frame's presentation time is reported, or only those
	 * of frames shown while their presentation is waited for. In the latter
	 * case frames are paced only while each starts after the previous one is
	 * shown, since only then is that presentation seen.
	 */
	void SetEveryPresentationReported(const bool reported);

	/**
	 * Return when the next frame should start and the vblank it aims at.
	 */
	SSchedule Schedule(const uint64_t now);

	/**
	 * Record that frame @p id started at @p time on @p schedule.
	 */
	void OnFrameStarted(const uint64_t id, const uint64_t time, const SSchedule& schedule);

	/**
	 * Record that the frame's CPU work ended with its present at @p time.
	 */
	void OnFramePresented(const uint64_t id, const uint64_t time);

	/**
	 * Record that the GPU finished the frame at @p time, where that's known.
	 * Call before OnFrameDisplayed for the same frame.
	 */
	void OnFrameReady(const uint64_t id, const uint64_t time);

	/**
	 * Record that the frame reached the display at @p time, or 0 when the
	 * time isn't known.
	 */
	void OnFrameDisplayed(const uint64_t id, const uint64_t time);

	/**
	 * Return the time after which the frame not being shown means it missed
	 * its vblank, or 0 when that isn't known or already recorded.
	 */
	uint64_t GetOverdueTime(const uint64_t id) const;

	/**
	 * Record that the frame wasn't shown by @p time.
	 */
	void OnFrameNotShownBy(const uint64_t id, const uint64_t time);

	uint64_t GetRefreshPeriod() const { return m_Period; }
	bool IsRefreshVariable() const { return m_Variable; }
	// Return how far apart frames are shown.
	uint64_t GetStep() const { return m_Steps * m_Period / StepsPerRefresh(); }
	uint64_t GetMisses() const { return m_Misses; }

private:
	// Enough frames for the longest span the pacer looks back over, at the
	// highest frame rates.
	static constexpr size_t COSTS = 1024;
	static constexpr size_t PERIOD_SAMPLES = 32;
	static constexpr size_t FRAMES = 16;
	static constexpr uint32_t MAX_INTERVAL = 8;
	static constexpr uint32_t VARIABLE_STEPS_PER_REFRESH = 16;
	static constexpr size_t OFFSET_SAMPLES = 32;

	struct SFrame
	{
		uint64_t id{0};
		// When asked to start, or when the previous frame presented if later.
		// Starting after that, as a thread that wakes late does, counts as
		// CPU time.
		uint64_t startable{0};
		uint64_t start{0};
		uint64_t deadline{0};
		uint64_t target{0};
		uint64_t presented{0};
		uint64_t ready{0};
		bool missed{false};
	};

	struct SCost
	{
		// When the frame presented.
		uint64_t time{0};
		// From when the frame could start to its present.
		uint64_t cpu{0};
		// From when the GPU could start on the frame to when it was ready.
		uint64_t gpu{0};
		// Whether it missed its vblank taking longer than the deepest lead.
		bool tooLong{false};
	};

	template<typename T, size_t N>
	struct SRing
	{
		std::array<T, N> values{};
		size_t size{0};
		size_t next{0};

		void Push(const T& value);
		// Return the @p i-th oldest value.
		const T& At(const size_t i) const { return values[(next + N - size + i) % N]; }
	};

	// Counts misses by episode: misses close together are one.
	struct SEpisodes
	{
		uint32_t count{0};
		uint64_t first{0};
		uint64_t last{0};

		// Record a miss of the frame presented at @p time.
		void Add(const uint64_t time);
	};

	struct SReplay
	{
		// The largest and the median lead of the frames that fit, plus the
		// latch requirement.
		uint64_t budget{0};
		uint64_t typical{0};
		SEpisodes misses;
		bool keepsUp{true};
	};

	// Return the index of the oldest frame presented at or after @p time.
	size_t FirstSince(const uint64_t time) const;
	/**
	 * Replay the frames presented since @p since at a step of @p step.
	 */
	SReplay Replay(const uint64_t step, const uint64_t since) const;
	uint32_t StepsPerRefresh() const { return m_Variable ? VARIABLE_STEPS_PER_REFRESH : 1; }
	uint64_t StepTime(const uint32_t steps) const { return steps * m_Period / StepsPerRefresh(); }
	void ChooseStep(const uint64_t now);
	// Return the episodes of frames presented since @p since that missed taking
	// longer than the deepest lead.
	uint32_t CountMisses(const uint64_t since) const;
	void EstimatePeriod(const uint64_t id, const uint64_t displayed);
	// Judge the frame by when it was shown, and return whether it missed its
	// vblank taking longer than the deepest lead.
	bool Judge(const SFrame& frame, const uint64_t displayed);
	void CountMiss(const uint64_t time);
	// Return how long after its vblank a frame is shown late.
	uint64_t LateAfter() const { return m_Variable ? m_Period / 4 : m_Period / 2; }
	// Record evidence at @p time that the refresh is @p variable, and switch
	// to it on enough.
	void Suggest(const bool variable, const uint64_t time);
	void SetVariable(const bool variable, const uint64_t time);
	// Return the earliest vblank the frame could be shown at, given the
	// vblanks earlier frames took.
	uint64_t ExpectedVblank(const SFrame& frame) const;
	// Record that frame @p id took @p vblank, and the frames after it later ones.
	void Take(const uint64_t id, const uint64_t vblank);

	uint64_t m_Period{0};
	bool m_PeriodGiven{false};
	RefreshMode m_Mode{RefreshMode::UNKNOWN};
	bool m_Variable{false};
	// Evidence for the other refresh mode, and when it was last seen.
	uint32_t m_Suggestions{0};
	uint64_t m_LastSuggestion{0};
	// How long after the time asked for the display shows a frame.
	SRing<uint64_t, OFFSET_SAMPLES> m_Offsets;
	uint64_t m_DisplayOffset{0};
	bool m_EveryPresentationReported{true};
	// Intervals between presentations, to estimate an unknown period.
	uint64_t m_LastDisplayed{0};
	uint64_t m_LastDisplayedID{0};
	SRing<uint64_t, PERIOD_SAMPLES> m_Deltas;

	std::array<SFrame, FRAMES> m_Frames{};
	SRing<SCost, COSTS> m_Costs;
	uint64_t m_LastReady{0};
	uint64_t m_LastPresented{0};

	uint64_t m_Anchor{0};
	// The latest deadline taken by a started frame or by a late present.
	uint64_t m_Consumed{0};
	// The latest frame whose vblank is known or estimated, and that vblank.
	uint64_t m_TakenID{0};
	uint64_t m_TakenVblank{0};

	// The step in sixteenths of a refresh for a variable refresh, in
	// refreshes otherwise.
	uint32_t m_Steps{1};
	// When the step last changed; whether it was shortened then and hasn't
	// held since; and how many times the least to wait before shortening it
	// again.
	uint64_t m_StepSince{0};
	bool m_Trying{false};
	uint32_t m_StepUpBackoff{1};
	uint64_t m_Latch{0};
	uint64_t m_Budget{0};
	uint64_t m_Typical{0};
	// Misses since the latch last rose, when the last one was, and when the
	// latch last decayed.
	uint32_t m_RecentMisses{0};
	uint64_t m_LastMiss{0};
	uint64_t m_LastDecay{0};
	uint64_t m_Misses{0};
};

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_FRAMEPACER
