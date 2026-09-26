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

#ifndef INCLUDED_RENDERER_BACKEND_ISWAPCHAIN
#define INCLUDED_RENDERER_BACKEND_ISWAPCHAIN

#include "renderer/backend/IDeviceObject.h"
#include "renderer/backend/IFramebuffer.h"

#include <cstdint>

namespace Renderer
{

namespace Backend
{

class ISwapChain : public IDeviceObject<ISwapChain>
{
public:
	/**
	 * @return True if we still can use the swapchain to present. A swapchain
	 * can become invalid on window resize or some other system event.
	 */
	virtual bool IsValid() const = 0;

	/**
	 * Acquires a backbuffer for rendering a frame.
	 *
	 * @return True if it was successfully acquired and we can render to it.
	 */
	virtual bool AcquireNextBackbuffer() = 0;

	/**
	 * Returns a framebuffer for the current backbuffer with the required
	 * attachment operations. It should not be called if the last
	 * AcquireNextBackbuffer call returned false.
	 *
	 * It's guaranteed that for the same acquired backbuffer this function returns
	 * a framebuffer with the same attachments and properties except load and
	 * store operations.
	 *
	 * @return The last successfully acquired framebuffer that wasn't
	 * presented.
	 */
	virtual IFramebuffer* GetCurrentBackbuffer(
		const AttachmentLoadOp colorAttachmentLoadOp,
		const AttachmentStoreOp colorAttachmentStoreOp,
		const AttachmentLoadOp depthStencilAttachmentLoadOp,
		const AttachmentStoreOp depthStencilAttachmentStoreOp) = 0;

	/**
	 * Presents the backbuffer to the swapchain queue to be flipped on a
	 * screen. Should be called only if the last AcquireNextBackbuffer call
	 * returned true.
	 */
	virtual void Present() = 0;

	/**
	 * Wait until the next frame should start: as late as still lets it reach
	 * the display on time, so it reads its input as late as possible. Return
	 * how long after the previous frame this one is meant to be shown, in
	 * seconds, or 0 when that isn't known. Backends that can't pace frames
	 * return 0 at once.
	 */
	virtual double WaitForFrameStart() { return 0.0; }
};

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_ISWAPCHAIN
