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

#include "SwapChain.h"

#include "lib/config2.h"
#include "lib/debug.h"
#include "lib/hash.h"
#include "ps/ConfigDB.h"
#include "ps/CLogger.h"
#include "ps/Profile.h"
#include "renderer/backend/gl/Device.h"

#include <SDL3/SDL_video.h>

namespace Renderer
{

namespace Backend
{

namespace GL
{

namespace
{

} // anonymous namespace

// static
std::unique_ptr<CSwapChain> CSwapChain::Create(
	CDevice* device, SDL_Window* window,
	int surfaceDrawableWidth, int surfaceDrawableHeight,
	const bool vsync)
{
	std::unique_ptr<CSwapChain> swapChain(new CSwapChain());
	swapChain->m_Device = device;
	swapChain->m_Window = window;
	swapChain->m_SurfaceDrawableWidth = surfaceDrawableWidth;
	swapChain->m_SurfaceDrawableHeight = surfaceDrawableHeight;
	SDL_GL_SetSwapInterval(vsync ? 1 : 0);
	return swapChain;
}

CSwapChain::CSwapChain() = default;

CSwapChain::~CSwapChain()
{
}

IDevice* CSwapChain::GetDevice()
{
	return m_Device;
}

bool CSwapChain::AcquireNextBackbuffer()
{
	ENSURE(!m_BackbufferAcquired);
	m_BackbufferAcquired = true;
	return true;
}

size_t CSwapChain::BackbufferKeyHash::operator()(const BackbufferKey& key) const
{
	size_t seed = 0;
	hash_combine(seed, std::get<0>(key));
	hash_combine(seed, std::get<1>(key));
	hash_combine(seed, std::get<2>(key));
	hash_combine(seed, std::get<3>(key));
	return seed;
}

IFramebuffer* CSwapChain::GetCurrentBackbuffer(
	const AttachmentLoadOp colorAttachmentLoadOp,
	const AttachmentStoreOp colorAttachmentStoreOp,
	const AttachmentLoadOp depthStencilAttachmentLoadOp,
	const AttachmentStoreOp depthStencilAttachmentStoreOp)
{
	const BackbufferKey key{
		colorAttachmentLoadOp, colorAttachmentStoreOp,
		depthStencilAttachmentLoadOp, depthStencilAttachmentStoreOp};
	auto it = m_Backbuffers.find(key);
	if (it == m_Backbuffers.end())
	{
		it = m_Backbuffers.emplace(key, CFramebuffer::CreateBackbuffer(
			m_Device, m_SurfaceDrawableWidth, m_SurfaceDrawableHeight,
			colorAttachmentLoadOp, colorAttachmentStoreOp,
			depthStencilAttachmentLoadOp, depthStencilAttachmentStoreOp)).first;
	}
	return it->second.get();
}

void CSwapChain::Present()
{
	ENSURE(m_BackbufferAcquired);
	m_BackbufferAcquired = false;

	if (m_Window)
	{
		PROFILE3("swap buffers");
		SDL_GL_SwapWindow(m_Window);
	}

#if defined(NDEBUG)
	if (!g_ConfigDB.Get("gl.checkerrorafterswap", false))
		return;
#endif
	PROFILE3("error check");
	// We have to check GL errors after SwapBuffer to avoid possible
	// synchronizations during rendering.
	if (GLenum err = glGetError())
		ONCE(LOGERROR("GL error 0x%04x occurred", err));
}

} // namespace GL

} // namespace Backend

} // namespace Renderer
