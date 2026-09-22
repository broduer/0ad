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

#ifndef INCLUDED_RENDERTHREAD
#define INCLUDED_RENDERTHREAD

#include <atomic>
#include <functional>

/**
 * Coordinates the main (logic) thread and the optional render thread.
 */
namespace RenderThread
{

/**
 * Return whether rendering has its own thread, read from the config once.
 */
bool IsThreaded();

/**
 * Return whether the calling thread renders: the render thread, or the main
 * thread without one.
 */
bool IsRenderThread();

/**
 * Return whether the calling thread is the one started by Start.
 */
bool IsDedicatedRenderThread();

/**
 * Start the render thread, running @p frame until Stop.
 */
void Start(std::function<void()> frame);
void Stop();

/**
 * Set how the device moves between threads, before Start: the running render
 * thread owns it and a paused one yields it to the logic thread.
 */
void SetDeviceHandoff(std::function<void()> bind, std::function<void()> unbind);

/**
 * Signal a new frame, adding its duration in seconds.
 */
void SignalFrame(double realTime);

/**
 * Wait for a new frame and return the time elapsed since the last call;
 * without a render thread, return the accumulated time at once.
 */
double WaitForFrames();

/**
 * Park the render thread between frames while in scope, so the logic thread
 * can use the device. Do nothing on the render thread itself.
 */
class Pause
{
public:
	Pause();
	~Pause();
	Pause(const Pause&) = delete;
	Pause& operator=(const Pause&) = delete;

private:
	bool m_Active;
};

} // namespace RenderThread

#endif // INCLUDED_RENDERTHREAD
