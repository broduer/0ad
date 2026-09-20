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

#ifndef INCLUDED_THREADAFFINITY
#define INCLUDED_THREADAFFINITY

/**
 * Give the render thread a core of its own and keep other threads off it.
 */
namespace ThreadAffinity
{
/**
 * Pin the calling thread, which builds and submits frames, to a core of its own
 * so its caches stay warm: the core the platform ranks highest, or the last
 * among equals, in the last physical core's cache domain, away from the
 * interrupts that tend to land on the first cores.
 */
void PinCurrentThreadToRenderCpu();

/**
 * Restore the mask saved by PinCurrentThreadToRenderCpu.
 * @return whether the thread was pinned and its mask was restored.
 */
bool RestoreCurrentThreadAffinity();

/**
 * Restore the calling thread's original affinity while in scope, so threads
 * created meanwhile don't inherit the render CPU.
 */
class ScopedUnpin
{
public:
	ScopedUnpin();
	~ScopedUnpin();
	ScopedUnpin(const ScopedUnpin&) = delete;
	ScopedUnpin& operator=(const ScopedUnpin&) = delete;

private:
	bool m_Repin;
};

/**
 * Keep the calling thread off the render core, within its cache domain if
 * that is large enough.
 */
void KeepCurrentThreadOffRenderCore();
}

#endif // INCLUDED_THREADAFFINITY
