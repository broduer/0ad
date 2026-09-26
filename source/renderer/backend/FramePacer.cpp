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

#include "FramePacer.h"

#include <algorithm>

namespace Renderer
{

namespace Backend
{

namespace
{

constexpr uint64_t MS{1'000'000};
// How long frames are measured before pacing, to have seen a scene start.
constexpr uint64_t MEASURE_BEFORE_PACING{1000 * MS};
// How far back the budget looks: enough turns to have seen a costly one.
constexpr uint64_t BUDGET_SPAN{3000 * MS};
// How many intervals before its deadline a frame may start, which is how deep
// frames queue for the display.
constexpr uint64_t MAX_LEAD_INTERVALS{3};
// Episodes of misses a step may have in a budget span.
constexpr uint32_t MAX_MISSES{2};
// A miss this soon after the one before belongs to its episode, which lasts
// this long at most. Misses that go on for a budget span make many episodes;
// a burst makes one or two.
constexpr uint64_t EPISODE_GAP{100 * MS};
constexpr uint64_t EPISODE_LENGTH{250 * MS};
// How long to keep a step, and to have it fit, before trying a shorter one,
// and the most times that to wait after a try failed.
constexpr uint64_t STEP_UP_SPAN{1000 * MS};
constexpr uint32_t MAX_STEP_UP_BACKOFF{8};
// How long after a miss it's forgotten, and how long on time the latch
// requirement takes to decay a step.
constexpr uint64_t MISS_MEMORY{400 * MS};
constexpr uint64_t LATCH_DECAY{3000 * MS};

uint64_t Before(const uint64_t time, const uint64_t span)
{
	return time > span ? time - span : 0;
}

// Return the least of @p lo to @p hi that @p fits, or @p hi if none does.
// Whatever fits, anything larger fits too.
template<typename Fits>
uint32_t Least(uint32_t lo, uint32_t hi, const Fits& fits)
{
	while (lo < hi)
	{
		const uint32_t mid{lo + (hi - lo) / 2};
		if (fits(mid))
			hi = mid;
		else
			lo = mid + 1;
	}
	return lo;
}

} // anonymous namespace

template<typename T, size_t N>
void CFramePacer::SRing<T, N>::Push(const T& value)
{
	values[next] = value;
	next = (next + 1) % N;
	size = std::min(size + 1, N);
}

void CFramePacer::SEpisodes::Add(const uint64_t time)
{
	if (count == 0 || time > last + EPISODE_GAP || time >= first + EPISODE_LENGTH)
	{
		++count;
		first = time;
	}
	last = time;
}

void CFramePacer::SetRefreshPeriod(const uint64_t period, const RefreshMode mode)
{
	m_PeriodGiven = true;
	m_Mode = mode;
	if (mode != RefreshMode::UNKNOWN && (mode == RefreshMode::VARIABLE) != m_Variable)
		SetVariable(mode == RefreshMode::VARIABLE, m_StepSince);
	if (period == m_Period)
		return;
	m_Period = period;
	m_Latch = 0;
}

void CFramePacer::SetEveryPresentationReported(const bool reported)
{
	m_EveryPresentationReported = reported;
}

CFramePacer::SSchedule CFramePacer::Schedule(const uint64_t now)
{
	SSchedule schedule;
	if (m_Period == 0 || m_Anchor == 0 || m_Costs.size == 0 ||
		m_Costs.At(0).time > Before(now, MEASURE_BEFORE_PACING))
	{
		return schedule;
	}
	// Unless the GPU's finishing is reported, the requirement covers it too.
	if (m_Latch == 0)
		m_Latch = m_EveryPresentationReported ? m_Period / 4 : m_Period / 2;

	ChooseStep(now);
	const uint64_t step{GetStep()};
	if (!m_EveryPresentationReported && m_Budget > step)
		return schedule;

	// Aim at the first vblank the frame can make and no other frame has
	// taken: each frame presented but not yet shown takes one after the last
	// presentation. A frame starting late aims at the vblank a typical frame
	// would make rather than skip one.
	const uint64_t pending{static_cast<uint64_t>(std::count_if(m_Frames.begin(), m_Frames.end(),
		[](const SFrame& frame) { return frame.presented != 0; }))};
	const uint64_t earliest{std::max({now + m_Typical, m_Consumed + step, m_Anchor + (pending + 1) * m_Period})};
	if (m_Variable)
	{
		schedule.deadline = earliest;
		schedule.target = earliest - m_DisplayOffset;
	}
	else
	{
		const uint64_t from{earliest - m_Period / 4};
		const uint64_t refreshes{from > m_Anchor ? (from - m_Anchor + m_Period - 1) / m_Period : 1};
		schedule.deadline = m_Anchor + std::max<uint64_t>(refreshes, 1) * m_Period;
		schedule.target = schedule.deadline;
		schedule.nearestRefresh = true;
	}
	schedule.start = schedule.deadline - m_Budget;
	return schedule;
}

size_t CFramePacer::FirstSince(const uint64_t time) const
{
	size_t first{m_Costs.size};
	while (first > 0 && m_Costs.At(first - 1).time >= time)
		--first;
	return first;
}

CFramePacer::SReplay CFramePacer::Replay(const uint64_t step, const uint64_t since) const
{
	SReplay replay;
	const uint64_t deepest{MAX_LEAD_INTERVALS * step};
	std::array<uint64_t, COSTS> leads;
	size_t fitting{0};
	// Relative to when the first frame was asked to start.
	uint64_t asked{0};
	uint64_t cpuFree{0};
	uint64_t gpuFree{0};
	uint64_t cpuTotal{0};
	uint64_t gpuTotal{0};
	const size_t first{FirstSince(since)};
	for (size_t i = first; i < m_Costs.size; ++i, asked += step)
	{
		const SCost& cost{m_Costs.At(i)};
		cpuTotal += std::min(cost.cpu, deepest);
		gpuTotal += std::min(cost.gpu, deepest);
		cpuFree = std::max(asked, cpuFree) + cost.cpu;
		gpuFree = std::max(cpuFree, gpuFree) + cost.gpu;
		const uint64_t lead{gpuFree - asked};
		if (lead + m_Latch <= deepest)
		{
			leads[fitting++] = lead;
			continue;
		}
		replay.misses.Add(cost.time);
		// The schedule aims the frames after a miss at later vblanks rather
		// than queue them behind it.
		asked = gpuFree - step;
	}
	// A step shorter than the frames' average cost queues them ever deeper,
	// which misses every so often however deep the queue. Over less than an
	// episode, that's a burst, and a frame longer than the deepest lead misses
	// at any step.
	const size_t frames{m_Costs.size - first};
	replay.keepsUp = frames == 0 || m_Costs.At(m_Costs.size - 1).time < m_Costs.At(first).time + EPISODE_LENGTH ||
		std::max(cpuTotal, gpuTotal) <= frames * step;
	if (fitting == 0)
	{
		replay.budget = deepest;
		replay.typical = deepest;
		return replay;
	}
	replay.budget = *std::max_element(leads.begin(), leads.begin() + fitting) + m_Latch;
	std::nth_element(leads.begin(), leads.begin() + fitting / 2, leads.begin() + fitting);
	replay.typical = leads[fitting / 2] + m_Latch;
	return replay;
}

uint32_t CFramePacer::CountMisses(const uint64_t since) const
{
	SEpisodes misses;
	for (size_t i = FirstSince(since); i < m_Costs.size; ++i)
		if (m_Costs.At(i).tooLong)
			misses.Add(m_Costs.At(i).time);
	return misses.count;
}

void CFramePacer::ChooseStep(const uint64_t now)
{
	const uint32_t shortest{StepsPerRefresh()};
	const uint32_t longest{MAX_INTERVAL * shortest};
	// The frames since the last change decide the step, so that the load that
	// changed it doesn't hold it after it's gone.
	const uint64_t since{std::max(m_StepSince, Before(now, BUDGET_SPAN))};
	const auto fits = [this, since](const uint32_t steps)
	{
		const SReplay replay{Replay(StepTime(steps), since)};
		return replay.keepsUp && replay.misses.count <= MAX_MISSES;
	};
	if (m_Steps < longest && (!fits(m_Steps) || CountMisses(since) > MAX_MISSES))
	{
		m_Steps = Least(m_Steps + 1, longest, fits);
		if (m_Trying)
			m_StepUpBackoff = std::min(m_StepUpBackoff * 2, MAX_STEP_UP_BACKOFF);
		m_Trying = false;
		m_StepSince = now;
	}
	else if (m_Trying && now >= m_StepSince + BUDGET_SPAN)
	{
		m_Trying = false;
		m_StepUpBackoff = 1;
	}
	else if (m_Steps > shortest && now >= m_StepSince + m_StepUpBackoff * STEP_UP_SPAN)
	{
		const uint64_t recent{Before(now, STEP_UP_SPAN)};
		const auto fitsFaster = [this, recent](const uint32_t steps)
		{
			const uint64_t step{StepTime(steps)};
			const SReplay replay{Replay(step, recent)};
			return replay.keepsUp && replay.misses.count == 0 && replay.budget + step <= MAX_LEAD_INTERVALS * step;
		};
		if (fitsFaster(m_Steps - 1))
		{
			m_Steps = Least(shortest, m_Steps - 1, fitsFaster);
			m_Trying = true;
			m_StepSince = now;
		}
	}
	// Every frame of the span, before and since a change, sets the budget.
	const SReplay window{Replay(GetStep(), Before(now, BUDGET_SPAN))};
	m_Budget = window.budget;
	m_Typical = std::min(window.typical, window.budget);
}

void CFramePacer::OnFrameStarted(const uint64_t id, const uint64_t time, const SSchedule& schedule)
{
	const uint64_t startable{schedule.deadline != 0 ? std::max(schedule.start, m_LastPresented) : time};
	m_Frames[id % FRAMES] = {id, std::min(startable, time), time, schedule.deadline, schedule.target, 0, 0, false};
	m_Consumed = std::max(m_Consumed, schedule.deadline);
}

void CFramePacer::OnFramePresented(const uint64_t id, const uint64_t time)
{
	SFrame& frame{m_Frames[id % FRAMES]};
	if (frame.id == id)
		frame.presented = time;
	m_LastPresented = std::max(m_LastPresented, time);
}

void CFramePacer::OnFrameReady(const uint64_t id, const uint64_t time)
{
	SFrame& frame{m_Frames[id % FRAMES]};
	if (frame.id == id)
		frame.ready = time;
}

void CFramePacer::OnFrameDisplayed(const uint64_t id, const uint64_t time)
{
	SFrame& frame{m_Frames[id % FRAMES]};
	const bool tracked{frame.id == id};
	// A frame is ready after it's presented and before it's shown; a time
	// outside that is on a miscalibrated clock.
	if (tracked && frame.ready != 0 && (frame.ready < frame.presented || (time != 0 && frame.ready > time)))
		frame.ready = 0;
	if (tracked && frame.start != 0 && frame.presented >= frame.start)
	{
		// The GPU starts on the frame once it's submitted and the previous
		// frame is done.
		const uint64_t gpuStart{std::max(frame.presented, m_LastReady)};
		SCost cost{frame.presented, frame.presented - frame.startable,
			frame.ready > gpuStart ? frame.ready - gpuStart : 0, false};
		if (time != 0)
			cost.tooLong = Judge(frame, time);
		m_Costs.Push(cost);
	}
	if (tracked && frame.ready != 0)
		m_LastReady = frame.ready;
	if (time != 0)
	{
		EstimatePeriod(id, time);
		m_Anchor = std::max(m_Anchor, time);
	}
	if (tracked)
		frame = {};
}

void CFramePacer::EstimatePeriod(const uint64_t id, const uint64_t displayed)
{
	const bool consecutive{id == m_LastDisplayedID + 1 && displayed > m_LastDisplayed && m_LastDisplayed != 0};
	const uint64_t delta{consecutive ? displayed - m_LastDisplayed : 0};
	m_LastDisplayed = displayed;
	m_LastDisplayedID = id;
	if (m_PeriodGiven || delta == 0)
		return;

	if (m_Period == 0)
	{
		// Unpaced frames at least as fast as the display land a refresh
		// apart; a low quantile of the intervals is robust to the rest.
		m_Deltas.Push(delta);
		if (m_Deltas.size == PERIOD_SAMPLES)
		{
			std::array<uint64_t, PERIOD_SAMPLES> sorted{m_Deltas.values};
			std::sort(sorted.begin(), sorted.end());
			m_Period = sorted[PERIOD_SAMPLES / 4];
		}
		return;
	}

	// Follow the period slowly from intervals of a few refreshes.
	const uint64_t refreshes{(delta + m_Period / 2) / m_Period};
	if (refreshes == 0 || refreshes > 4)
		return;
	const int64_t error{static_cast<int64_t>(delta / refreshes) - static_cast<int64_t>(m_Period)};
	m_Period = static_cast<uint64_t>(static_cast<int64_t>(m_Period) + error / 32);
}

uint64_t CFramePacer::GetOverdueTime(const uint64_t id) const
{
	const SFrame& frame{m_Frames[id % FRAMES]};
	if (frame.id != id || frame.missed || frame.deadline == 0 || m_Period == 0)
		return 0;
	return ExpectedVblank(frame) + LateAfter();
}

void CFramePacer::OnFrameNotShownBy(const uint64_t id, const uint64_t time)
{
	const uint64_t overdue{GetOverdueTime(id)};
	if (overdue == 0 || time <= overdue)
		return;
	SFrame& frame{m_Frames[id % FRAMES]};
	frame.missed = true;
	CountMiss(time);
	// It takes a vblank no earlier than the first after that, or for a
	// variable refresh, no earlier than a refresh after the one it missed.
	if (m_Variable)
	{
		Take(id, std::max(time, ExpectedVblank(frame) + m_Period));
		return;
	}
	const uint64_t refreshes{time > m_Anchor ? (time - m_Anchor + m_Period - 1) / m_Period : 0};
	Take(id, m_Anchor + refreshes * m_Period);
}

uint64_t CFramePacer::ExpectedVblank(const SFrame& frame) const
{
	if (m_TakenID == 0 || frame.id <= m_TakenID)
		return frame.deadline;
	return std::max(frame.deadline, m_TakenVblank + (frame.id - m_TakenID) * m_Period);
}

void CFramePacer::Take(const uint64_t id, const uint64_t vblank)
{
	if (id < m_TakenID)
		return;
	m_TakenID = id;
	m_TakenVblank = vblank;
	// In order, each frame started after it takes a later vblank.
	const uint64_t after{static_cast<uint64_t>(std::count_if(m_Frames.begin(), m_Frames.end(),
		[id](const SFrame& frame) { return frame.id > id; }))};
	m_Consumed = std::max(m_Consumed, vblank + after * m_Period);
}

void CFramePacer::CountMiss(const uint64_t time)
{
	// One late frame is jitter; two close together mean the display needs
	// frames earlier. Unless the GPU's finishing is reported, the requirement
	// covers its variation too, so every miss counts.
	++m_Misses;
	if (time > m_LastMiss + MISS_MEMORY)
		m_RecentMisses = 0;
	m_LastMiss = m_LastDecay = time;
	if (++m_RecentMisses >= (m_EveryPresentationReported ? 2u : 1u))
	{
		m_RecentMisses = 0;
		m_Latch = std::min(m_Latch + m_Period / 32, m_Period * 3 / 4);
	}
}

void CFramePacer::Suggest(const bool variable, const uint64_t time)
{
	if (time > m_LastSuggestion + BUDGET_SPAN)
		m_Suggestions = 0;
	m_LastSuggestion = time;
	if (++m_Suggestions >= 2)
		SetVariable(variable, time);
}

void CFramePacer::SetVariable(const bool variable, const uint64_t time)
{
	m_Steps = variable ? m_Steps * VARIABLE_STEPS_PER_REFRESH :
		(m_Steps + VARIABLE_STEPS_PER_REFRESH - 1) / VARIABLE_STEPS_PER_REFRESH;
	m_Variable = variable;
	m_Suggestions = 0;
	m_Offsets = {};
	m_DisplayOffset = 0;
	m_Trying = false;
	m_StepUpBackoff = 1;
	m_StepSince = time;
}

bool CFramePacer::Judge(const SFrame& frame, const uint64_t displayed)
{
	if (frame.deadline == 0 || m_Period == 0)
		return false;
	// Only presentation times reported by the display are exact enough to
	// tell the refresh mode by.
	const bool detect{m_Mode == RefreshMode::UNKNOWN && m_EveryPresentationReported};
	// A fixed refresh shows frames whole refreshes apart.
	if (detect && !m_Variable && m_Anchor != 0 && displayed > m_Anchor)
	{
		const uint64_t phase{(displayed - m_Anchor) % m_Period};
		if (std::min(phase, m_Period - phase) > m_Period / 4)
			Suggest(true, displayed);
	}
	// How long after the time asked for the display shows a frame ready by
	// then.
	const bool readyInTime{frame.ready != 0 && frame.ready + m_Latch <= frame.target};
	if (m_Variable && readyInTime && !frame.missed && displayed >= frame.target &&
		displayed <= frame.target + m_Period / 2)
	{
		m_Offsets.Push(displayed - frame.target);
		std::array<uint64_t, OFFSET_SAMPLES> offsets{m_Offsets.values};
		std::nth_element(offsets.begin(), offsets.begin() + m_Offsets.size / 2, offsets.begin() + m_Offsets.size);
		m_DisplayOffset = offsets[m_Offsets.size / 2];
	}

	// A frame shown late only because an earlier one took its vblank was
	// displaced, not late itself; one late though ready in time was late for
	// the display's sake.
	const uint64_t expected{ExpectedVblank(frame)};
	Take(frame.id, displayed);
	if (frame.missed || displayed > expected + LateAfter())
	{
		if (!frame.missed)
			CountMiss(displayed);
		const bool lackedTime{frame.ready == 0 || frame.ready + m_Latch > expected};
		// A variable refresh shows a frame ready in time when asked.
		if (detect && m_Variable && !lackedTime)
			Suggest(false, displayed);
		return lackedTime &&
			(frame.ready == 0 || frame.ready - frame.startable + m_Latch > MAX_LEAD_INTERVALS * GetStep());
	}
	if (displayed > frame.deadline + LateAfter())
		return false;
	if (displayed >= m_LastDecay + LATCH_DECAY)
	{
		// Give back far more slowly than taken, so a one-off doesn't cost
		// latency for the rest of the session.
		m_LastDecay = displayed;
		m_Latch = std::max(m_Latch * 31 / 32, m_Period / 32);
	}
	return false;
}

} // namespace Backend

} // namespace Renderer
