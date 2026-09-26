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

#include "graphics/Color.h"
#include "lib/debug.h"
#include "lib/hash.h"
#include "maths/MathUtil.h"
#include "ps/CLogger.h"
#include "ps/ConfigDB.h"
#include "ps/Profile.h"
#include "renderer/backend/ITexture.h"
#include "renderer/backend/Sampler.h"
#include "renderer/backend/vulkan/Device.h"
#include "renderer/backend/vulkan/DeviceSelection.h"
#include "renderer/backend/vulkan/Framebuffer.h"
#include "renderer/backend/vulkan/RingCommandContext.h"
#include "renderer/backend/vulkan/Texture.h"
#include "renderer/backend/vulkan/Utilities.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <limits>
#include <thread>
#include <SDL_video.h>
#include <utility>

namespace Renderer
{

namespace Backend
{

namespace Vulkan
{

namespace
{

// Return the monotonic clock's time in nanoseconds.
uint64_t Now()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Sleep until @p time on the monotonic clock.
void SleepUntil(const uint64_t time)
{
	std::this_thread::sleep_until(std::chrono::steady_clock::time_point{std::chrono::nanoseconds{time}});
}

} // anonymous namespace

// static
std::unique_ptr<CSwapChain> CSwapChain::Create(
	CDevice* device, CSubmitScheduler* submitScheduler,
	const uint32_t queueFamilyIndex, VkQueue queue,
	const char* name, VkSurfaceKHR surface,
	int surfaceDrawableWidth, int surfaceDrawableHeight,
	const bool vsync, std::unique_ptr<ISwapChain> oldSwapChain)
{
	// We don't need to wait to destroy the depth texture as it's independent
	// from VkSwapchainKHR.
	if (oldSwapChain)
	{
		oldSwapChain->As<CSwapChain>()->m_DepthTexture.reset();

		// Workaround to free the depth texture immediately.
		device->OnSwapChainRecreation();
	}

	// It seems some drivers might not reuse the same swapchain memory. So
	// to avoid higher memory peaks destroy the old swapchain before.
	const bool destroyOldSwapchainBefore{
		g_ConfigDB.Get("renderer.backend.vulkan.destroyoldswapchainbefore", false)};
	if (destroyOldSwapchainBefore)
		oldSwapChain.reset();

	VkPhysicalDevice physicalDevice = device->GetChoosenPhysicalDevice().device;

	VkSurfaceCapabilitiesKHR surfaceCapabilities{};
	ENSURE_VK_SUCCESS(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		physicalDevice, surface, &surfaceCapabilities));

	const uint32_t swapChainWidth = Clamp<uint32_t>(surfaceDrawableWidth,
		surfaceCapabilities.minImageExtent.width,
		surfaceCapabilities.maxImageExtent.width);
	const uint32_t swapChainHeight = Clamp<uint32_t>(surfaceDrawableHeight,
		surfaceCapabilities.minImageExtent.height,
		surfaceCapabilities.maxImageExtent.height);

	// Some drivers (for example NVIDIA on Windows during minimize) might
	// return zeroes for both minImageExtent and maxImageExtent. It means we're
	// not able to create any swapchain. Because we can't choose zeros (they're
	// not allowed) and we can't choose values bigger than maxImageExtent
	// (which are also zeroes in that case).
	if (swapChainWidth == 0 || swapChainHeight == 0)
		return nullptr;

	std::vector<VkSurfaceFormatKHR> surfaceFormats;
	uint32_t surfaceFormatCount = 0;
	ENSURE_VK_SUCCESS(vkGetPhysicalDeviceSurfaceFormatsKHR(
		physicalDevice, surface, &surfaceFormatCount, nullptr));
	if (surfaceFormatCount > 0)
	{
		surfaceFormats.resize(surfaceFormatCount);
		ENSURE_VK_SUCCESS(vkGetPhysicalDeviceSurfaceFormatsKHR(
			physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data()));
	}

	std::vector<VkPresentModeKHR> presentModes;
	uint32_t presentModeCount = 0;
	ENSURE_VK_SUCCESS(vkGetPhysicalDeviceSurfacePresentModesKHR(
		physicalDevice, surface, &presentModeCount, nullptr));
	if (presentModeCount > 0)
	{
		presentModes.resize(presentModeCount);
		ENSURE_VK_SUCCESS(vkGetPhysicalDeviceSurfacePresentModesKHR(
			physicalDevice, surface, &presentModeCount, presentModes.data()));
	}

	// VK_PRESENT_MODE_FIFO_KHR is guaranteed to be supported.
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	auto isPresentModeAvailable = [&presentModes](const VkPresentModeKHR presentMode)
	{
		return std::find(presentModes.begin(), presentModes.end(), presentMode) != presentModes.end();
	};
	if (vsync)
	{
		// TODO: use the adaptive one when possible.
		// https://gitlab.freedesktop.org/mesa/mesa/-/issues/5516
		//if (isPresentModeAvailable(VK_PRESENT_MODE_MAILBOX_KHR))
		//	presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
	}
	else
	{
		if (isPresentModeAvailable(VK_PRESENT_MODE_IMMEDIATE_KHR))
			presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	// Spec says:
	//   The number of format pairs supported must be greater than or equal to 1.
	//   pSurfaceFormats must not contain an entry whose value for format is
	//   VK_FORMAT_UNDEFINED.
	const auto surfaceFormatIt =
		std::find_if(surfaceFormats.begin(), surfaceFormats.end(), IsSurfaceFormatSupported);
	if (surfaceFormatIt == surfaceFormats.end())
	{
		LOGERROR("Can't find a suitable surface format to render to.");
		return nullptr;
	}
	const VkSurfaceFormatKHR& surfaceFormat = *surfaceFormatIt;

	VkSwapchainCreateInfoKHR swapChainCreateInfo{};
	swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChainCreateInfo.surface = surface;
	// minImageCount + 1 is to have a less chance for a presenter to wait.
	// maxImageCount might be zero, it means it's unlimited.
	const uint32_t maxImageCount = surfaceCapabilities.maxImageCount > 0
		? surfaceCapabilities.maxImageCount
		: std::numeric_limits<uint32_t>::max();
	const uint32_t minImageCount = surfaceCapabilities.minImageCount < maxImageCount
		? surfaceCapabilities.minImageCount + 1
		: surfaceCapabilities.minImageCount;
	// Without vsync the compositor can hold two images, the one on screen and
	// the latest presented, and each frame in flight holds another. Spare
	// images keep acquire from waiting on the compositor, and since immediate
	// presents never queue, they add no latency.
	const uint32_t spareImageCount{presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? 3u : 0u};
	swapChainCreateInfo.minImageCount =
		Clamp<uint32_t>(NUMBER_OF_FRAMES_IN_FLIGHT + spareImageCount,
			minImageCount, maxImageCount);
	swapChainCreateInfo.imageFormat = surfaceFormat.format;
	swapChainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapChainCreateInfo.imageExtent.width = swapChainWidth;
	swapChainCreateInfo.imageExtent.height = swapChainHeight;
	swapChainCreateInfo.imageArrayLayers = 1;
	// VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT is guaranteed to present.
	// VK_IMAGE_USAGE_TRANSFER_SRC_BIT allows a simpler backbuffer readback.
	// VK_IMAGE_USAGE_TRANSFER_DST_BIT allows a blit to the backbuffer.
	// VK_IMAGE_USAGE_STORAGE_BIT allows to write to the backbuffer directly
	// from a compute shader.
	swapChainCreateInfo.imageUsage =
		(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT) &
			surfaceCapabilities.supportedUsageFlags;
	swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	// We need to set these only if imageSharingMode is VK_SHARING_MODE_CONCURRENT.
	swapChainCreateInfo.queueFamilyIndexCount = 0;
	swapChainCreateInfo.pQueueFamilyIndices = nullptr;
	// By default VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR is preferable.
	if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		swapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else
		swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	// By default VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR is preferable, other bits
	// might require some format or rendering adjustemnts to avoid
	// semi-transparent areas.
	const VkCompositeAlphaFlagBitsKHR compositeAlphaOrder[] =
	{
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
	};
	for (const VkCompositeAlphaFlagBitsKHR compositeAlpha : compositeAlphaOrder)
		if (compositeAlpha & surfaceCapabilities.supportedCompositeAlpha)
		{
			swapChainCreateInfo.compositeAlpha = compositeAlpha;
			break;
		}
	swapChainCreateInfo.presentMode = presentMode;
	swapChainCreateInfo.clipped = VK_TRUE;
	if (oldSwapChain)
		swapChainCreateInfo.oldSwapchain = oldSwapChain->As<CSwapChain>()->GetVkSwapchain();

	std::unique_ptr<CSwapChain> swapChain(new CSwapChain());
	swapChain->m_Device = device;
	swapChain->m_SubmitScheduler = submitScheduler;
	swapChain->m_Queue = queue;

	// With vsync, frames are paced by waiting on presents, and scheduled by
	// present timing where the surface can report and honour it.
	VkPresentTimingSurfaceCapabilitiesEXT presentTimingCapabilities{};
	presentTimingCapabilities.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT;
	if (presentMode == VK_PRESENT_MODE_FIFO_KHR && device->UsePresentWait2())
	{
		VkSurfaceCapabilitiesPresentId2KHR presentId2Capabilities{};
		presentId2Capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR;
		VkSurfaceCapabilitiesPresentWait2KHR presentWait2Capabilities{};
		presentWait2Capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR;
		presentId2Capabilities.pNext = &presentWait2Capabilities;
		if (device->UsePresentTiming())
			presentWait2Capabilities.pNext = &presentTimingCapabilities;
		VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo{};
		surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
		surfaceInfo.surface = surface;
		VkSurfaceCapabilities2KHR surfaceCapabilities2{};
		surfaceCapabilities2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
		surfaceCapabilities2.pNext = &presentId2Capabilities;
		if (vkGetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice, &surfaceInfo, &surfaceCapabilities2) == VK_SUCCESS &&
			presentId2Capabilities.presentId2Supported && presentWait2Capabilities.presentWait2Supported)
		{
			swapChain->m_PresentWait = PresentWait::V2;
			swapChainCreateInfo.flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
			const VkPresentStageFlagsEXT displayStages{
				VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT | VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT};
			if (device->UsePresentTiming() && presentTimingCapabilities.presentTimingSupported &&
				presentTimingCapabilities.presentAtAbsoluteTimeSupported &&
				(presentTimingCapabilities.presentStageQueries & displayStages))
			{
				swapChain->m_PresentTiming = true;
				swapChainCreateInfo.flags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
			}
		}
	}
	if (presentMode == VK_PRESENT_MODE_FIFO_KHR && swapChain->m_PresentWait == PresentWait::NONE &&
		device->UsePresentWait())
	{
		swapChain->m_PresentWait = PresentWait::V1;
	}

	RETURN_NULLPTR_IF_NOT_VK_SUCCESS(vkCreateSwapchainKHR(
		device->GetVkDevice(), &swapChainCreateInfo, nullptr, &swapChain->m_SwapChain));

	device->SetObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, swapChain->m_SwapChain, name);

	uint32_t imageCount = 0;
	VkResult getSwapchainImagesResult = VK_INCOMPLETE;
	do
	{
		getSwapchainImagesResult = vkGetSwapchainImagesKHR(
			device->GetVkDevice(), swapChain->m_SwapChain, &imageCount, nullptr);
		if (getSwapchainImagesResult == VK_SUCCESS && imageCount > 0)
		{
			swapChain->m_Images.resize(imageCount);
			getSwapchainImagesResult = vkGetSwapchainImagesKHR(
				device->GetVkDevice(), swapChain->m_SwapChain, &imageCount, swapChain->m_Images.data());
		}
	} while (getSwapchainImagesResult == VK_INCOMPLETE);
	LOGMESSAGE("SwapChain image count: %u (min: %u)", imageCount, swapChainCreateInfo.minImageCount);
	if (swapChain->m_PresentTiming)
		swapChain->SetUpPresentTiming(presentTimingCapabilities);
	swapChain->m_Pacer.SetEveryPresentationReported(swapChain->m_PresentTiming);
	LOGMESSAGE("SwapChain frame pacing: %s",
		swapChain->m_PresentTiming ? "present timing" :
		swapChain->m_PresentWait != PresentWait::NONE ? "present wait" : "none");
	ENSURE_VK_SUCCESS(getSwapchainImagesResult);
	ENSURE(imageCount > 0);

	// We create a depth texture to mimic GL behavior where SDL creates it for
	// a backbuffer.
	swapChain->m_DepthTexture = CTexture::Create(
		device, "SwapChainDepthTexture", ITexture::Type::TEXTURE_2D,
		ITexture::Usage::DEPTH_STENCIL_ATTACHMENT,
		device->GetPreferredDepthStencilFormat(
			Renderer::Backend::ITexture::Usage::DEPTH_STENCIL_ATTACHMENT,
			true, true),
		swapChainWidth, swapChainHeight, Sampler::MakeDefaultSampler(
			Sampler::Filter::NEAREST, Sampler::AddressMode::CLAMP_TO_EDGE),
		1, 1);

	swapChain->m_ImageFormat = swapChainCreateInfo.imageFormat;

	swapChain->m_Textures.resize(imageCount);
	swapChain->m_Backbuffers.resize(imageCount);
	char nameBuffer[64];
	for (size_t index = 0; index < imageCount; ++index)
	{
		snprintf(nameBuffer, std::size(nameBuffer), "SwapChainImage #%zu", index);
		device->SetObjectName(VK_OBJECT_TYPE_IMAGE, swapChain->m_Images[index], nameBuffer);
		snprintf(nameBuffer, std::size(nameBuffer), "SwapChainImageView #%zu", index);
		swapChain->m_Textures[index] = CTexture::WrapBackbufferImage(
			device, nameBuffer, swapChain->m_Images[index], swapChainCreateInfo.imageFormat,
			swapChainCreateInfo.imageUsage, swapChainWidth, swapChainHeight);
	}

	// +1 to minimize waiting on our side instead of vkAcquireNextImageKHR.
	for (size_t index{0}; index < NUMBER_OF_FRAMES_IN_FLIGHT + 1; ++index)
	{
		VkSemaphore semaphore{VK_NULL_HANDLE};

		VkSemaphoreCreateInfo semaphoreCreateInfo{};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		RETURN_NULLPTR_IF_NOT_VK_SUCCESS(vkCreateSemaphore(
			device->GetVkDevice(), &semaphoreCreateInfo, nullptr, &semaphore));
		snprintf(nameBuffer, std::size(nameBuffer), "SwapChainAcquireSemaphore #%zu", index);
		device->SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, semaphore, nameBuffer);

		swapChain->m_FrameObjects.emplace_back(FrameObject{semaphore, CSubmitScheduler::INVALID_SUBMIT_HANDLE});
	}

	for (size_t index{0}; index < imageCount; ++index)
	{
		VkSemaphore semaphore{VK_NULL_HANDLE};

		VkSemaphoreCreateInfo semaphoreCreateInfo{};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		RETURN_NULLPTR_IF_NOT_VK_SUCCESS(vkCreateSemaphore(
			device->GetVkDevice(), &semaphoreCreateInfo, nullptr, &semaphore));
		snprintf(nameBuffer, std::size(nameBuffer), "SwapChainSubmitSemaphore #%zu", index);
		device->SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, semaphore, nameBuffer);

		swapChain->m_SubmitSemaphores.emplace_back(semaphore);
	}

	swapChain->m_AcquireCommandContext = CRingCommandContext::Create(
		device, NUMBER_OF_FRAMES_IN_FLIGHT, queueFamilyIndex, *submitScheduler);
	if (!swapChain->m_AcquireCommandContext)
		return nullptr;
	swapChain->m_PresentCommandContext = CRingCommandContext::Create(
		device, NUMBER_OF_FRAMES_IN_FLIGHT, queueFamilyIndex, *submitScheduler);
	if (!swapChain->m_PresentCommandContext)
		return nullptr;

	swapChain->m_DebugWaitIdleBeforeAcquire = g_ConfigDB.Get(
		"renderer.backend.vulkan.debugwaitidlebeforeacquire",
		swapChain->m_DebugWaitIdleBeforeAcquire);
	swapChain->m_DebugWaitIdleBeforePresent = g_ConfigDB.Get(
		"renderer.backend.vulkan.debugwaitidlebeforepresent",
		swapChain->m_DebugWaitIdleBeforePresent);
	swapChain->m_DebugWaitIdleAfterPresent = g_ConfigDB.Get(
		"renderer.backend.vulkan.debugwaitidleafterpresent",
		swapChain->m_DebugWaitIdleAfterPresent);

	swapChain->m_IsValid = true;

	return swapChain;
}

CSwapChain::CSwapChain() = default;

CSwapChain::~CSwapChain()
{
	VkDevice device{m_Device->GetVkDevice()};

	m_Backbuffers.clear();

	m_Textures.clear();
	m_DepthTexture.reset();

	if (m_SwapChain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(device, m_SwapChain, nullptr);

	for (FrameObject& frameObject : m_FrameObjects)
	{
		if (frameObject.submitHandle != CSubmitScheduler::INVALID_SUBMIT_HANDLE)
			m_SubmitScheduler->WaitUntilFree(frameObject.submitHandle);

		if (frameObject.acquireImageSemaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(device, frameObject.acquireImageSemaphore, nullptr);
	}

	for (VkSemaphore& semaphore : m_SubmitSemaphores)
		if (semaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(device, semaphore, nullptr);
}

IDevice* CSwapChain::GetDevice()
{
	return m_Device;
}

bool CSwapChain::AcquireNextBackbuffer()
{
	ENSURE(m_IsValid);

	PROFILE3("AcquireNextBackbuffer");

	// We need to make sure we can reuse the semaphore.
	FrameObject& frameObject{m_FrameObjects[m_FrameID % m_FrameObjects.size()]};
	if (frameObject.submitHandle != CSubmitScheduler::INVALID_SUBMIT_HANDLE)
	{
		PROFILE3("WaitAcquireSemaphore");
		m_SubmitScheduler->WaitUntilFree(frameObject.submitHandle);
	}

	if (m_DebugWaitIdleBeforeAcquire)
		vkDeviceWaitIdle(m_Device->GetVkDevice());

	if (!AcquireNextImage())
		return false;
	SubmitCommandsAfterAcquireNextImage(*m_AcquireCommandContext);

	m_SubmitScheduler->EnqueueWaitOnNextSubmit(
		frameObject.acquireImageSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	m_AcquireCommandContext->Flush();
	return true;
}

void CSwapChain::Present()
{
	ENSURE(m_IsValid);
	ENSURE(m_CurrentImageIndex != std::numeric_limits<uint32_t>::max());
	ENSURE(m_CurrentImageIndex < m_SubmitSemaphores.size());

	PROFILE3("Present");
	FrameObject& frameObject{m_FrameObjects[m_FrameID % m_FrameObjects.size()]};

	SubmitCommandsBeforePresent(*m_PresentCommandContext);
	m_SubmitScheduler->EnqueueSignalOnNextSubmit(m_SubmitSemaphores[m_CurrentImageIndex]);
	m_PresentCommandContext->Flush();
	frameObject.submitHandle = m_SubmitScheduler->Flush();

	if (m_DebugWaitIdleBeforePresent)
		vkDeviceWaitIdle(m_Device->GetVkDevice());

	Present(m_SubmitSemaphores[m_CurrentImageIndex], m_Queue);

	if (m_DebugWaitIdleAfterPresent)
		vkDeviceWaitIdle(m_Device->GetVkDevice());

	m_Device->OnPresent();
	++m_FrameID;
}

size_t CSwapChain::SwapChainBackbuffer::BackbufferKeyHash::operator()(const BackbufferKey& key) const
{
	size_t seed = 0;
	hash_combine(seed, std::get<0>(key));
	hash_combine(seed, std::get<1>(key));
	hash_combine(seed, std::get<2>(key));
	hash_combine(seed, std::get<3>(key));
	return seed;
}

CSwapChain::SwapChainBackbuffer::SwapChainBackbuffer() = default;

CSwapChain::SwapChainBackbuffer::SwapChainBackbuffer(SwapChainBackbuffer&& other) = default;

CSwapChain::SwapChainBackbuffer& CSwapChain::SwapChainBackbuffer::operator=(SwapChainBackbuffer&& other) = default;

bool CSwapChain::AcquireNextImage()
{
	ENSURE(m_IsValid);
	ENSURE(m_CurrentImageIndex == std::numeric_limits<uint32_t>::max());

	VkSemaphore acquireImageSemaphore{m_FrameObjects[m_FrameID % m_FrameObjects.size()].acquireImageSemaphore};
	const VkResult acquireResult = vkAcquireNextImageKHR(
		m_Device->GetVkDevice(), m_SwapChain, std::numeric_limits<uint64_t>::max(),
		acquireImageSemaphore,
		VK_NULL_HANDLE, &m_CurrentImageIndex);
	if (acquireResult != VK_SUCCESS)
	{
		if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
			m_IsValid = false;
		else if (acquireResult != VK_SUBOPTIMAL_KHR)
		{
			LOGERROR("Acquire result: %d (%s)",
				static_cast<int>(acquireResult), Utilities::GetVkResultName(acquireResult));
			debug_warn("Unknown acquire error.");
		}
	}
	return m_IsValid;
}

void CSwapChain::SubmitCommandsAfterAcquireNextImage(
	CRingCommandContext& commandContext)
{
	const bool firstAcquirement = !m_Textures[m_CurrentImageIndex]->IsInitialized();
	Utilities::SubmitImageMemoryBarrier(
		commandContext.GetCommandBuffer(),
		m_Images[m_CurrentImageIndex], 0, 0,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		firstAcquirement ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	if (!m_DepthTexture->IsInitialized())
	{
		Utilities::SubmitImageMemoryBarrier(
			commandContext.GetCommandBuffer(),
			m_DepthTexture->GetImage(), 0, 0,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
	}
}

void CSwapChain::SubmitCommandsBeforePresent(
	CRingCommandContext& commandContext)
{
	ENSURE(m_CurrentImageIndex != std::numeric_limits<uint32_t>::max());

	Utilities::SubmitImageMemoryBarrier(
		commandContext.GetCommandBuffer(), m_Images[m_CurrentImageIndex], 0, 0,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void CSwapChain::Present(VkSemaphore submitDone, VkQueue queue)
{
	ENSURE(m_IsValid);
	ENSURE(m_CurrentImageIndex != std::numeric_limits<uint32_t>::max());

	VkSwapchainKHR swapChains[] = {m_SwapChain};

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &m_CurrentImageIndex;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &submitDone;
	const void** next{&presentInfo.pNext};
	const auto chain = [&next](auto& info)
	{
		*next = &info;
		next = &info.pNext;
	};
	VkPresentIdKHR presentId{};
	presentId.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
	VkPresentId2KHR presentId2{};
	presentId2.sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR;
	if (m_PresentWait != PresentWait::NONE)
	{
		++m_PresentID;
		if (m_PresentWait == PresentWait::V2)
		{
			presentId2.swapchainCount = 1;
			presentId2.pPresentIds = &m_PresentID;
			chain(presentId2);
		}
		else
		{
			presentId.swapchainCount = 1;
			presentId.pPresentIds = &m_PresentID;
			chain(presentId);
		}
		m_Pacer.OnFramePresented(m_PresentID, Now());
	}
	VkPresentTimingInfoEXT timingInfo{};
	timingInfo.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT;
	VkPresentTimingsInfoEXT timingsInfo{};
	timingsInfo.sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT;
	if (m_PresentTiming)
	{
		timingInfo.timeDomainId = m_TimeDomainID;
		if (m_Schedule.deadline != 0)
		{
			timingInfo.targetTime = m_Schedule.target + m_ShownOffset;
			if (m_Schedule.nearestRefresh)
				timingInfo.flags = VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
			if (m_TimeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT)
				timingInfo.targetTimeDomainPresentStage = m_ShownStage;
		}
		// Each present asking for timings holds a result until it's read.
		if (m_PendingTimings < m_Timings.size())
		{
			timingInfo.presentStageQueries = m_ReadyStage | m_ShownStage;
			++m_PendingTimings;
		}
		timingsInfo.swapchainCount = 1;
		timingsInfo.pTimingInfos = &timingInfo;
		chain(timingsInfo);
	}
	const VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);
	if (presentResult != VK_SUCCESS)
	{
		if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
			m_IsValid = false;
		else if (presentResult != VK_SUBOPTIMAL_KHR)
		{
			LOGERROR("Present result: %d (%s)",
				static_cast<int>(presentResult), Utilities::GetVkResultName(presentResult));
			debug_warn("Unknown present error.");
		}
	}

	m_CurrentImageIndex = std::numeric_limits<uint32_t>::max();
}

void CSwapChain::SetUpPresentTiming(const VkPresentTimingSurfaceCapabilitiesEXT& capabilities)
{
	m_ReadyStage = capabilities.presentStageQueries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT;
	m_ShownStage = (capabilities.presentStageQueries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) ?
		VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT : VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT;

	// Times are converted to the monotonic clock, so it must be calibrateable.
	const VkPhysicalDevice physicalDevice{m_Device->GetChoosenPhysicalDevice().device};
	uint32_t calibrateableCount{0};
	vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, &calibrateableCount, nullptr);
	std::vector<VkTimeDomainKHR> calibrateable(calibrateableCount);
	vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(physicalDevice, &calibrateableCount, calibrateable.data());
	constexpr uint32_t TIMING_QUEUE_SIZE{32};
	m_PresentTiming =
		std::find(calibrateable.begin(), calibrateable.end(), VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR) != calibrateable.end() &&
		vkSetSwapchainPresentTimingQueueSizeEXT(m_Device->GetVkDevice(), m_SwapChain, TIMING_QUEUE_SIZE) == VK_SUCCESS &&
		ChooseTimeDomain();
	if (!m_PresentTiming)
		return;

	constexpr size_t STAGES{2};
	m_Timings.resize(TIMING_QUEUE_SIZE);
	m_StageTimes.resize(TIMING_QUEUE_SIZE * STAGES);
	for (size_t index = 0; index < m_Timings.size(); ++index)
	{
		m_Timings[index].sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
		m_Timings[index].pPresentStages = m_StageTimes.data() + index * STAGES;
	}
}

bool CSwapChain::ChooseTimeDomain()
{
	VkSwapchainTimeDomainPropertiesEXT properties{};
	properties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT;
	if (vkGetSwapchainTimeDomainPropertiesEXT(m_Device->GetVkDevice(), m_SwapChain, &properties, nullptr) != VK_SUCCESS)
		return false;
	std::vector<VkTimeDomainKHR> domains(properties.timeDomainCount);
	std::vector<uint64_t> domainIDs(properties.timeDomainCount);
	properties.pTimeDomains = domains.data();
	properties.pTimeDomainIds = domainIDs.data();
	if (vkGetSwapchainTimeDomainPropertiesEXT(
		m_Device->GetVkDevice(), m_SwapChain, &properties, &m_TimeDomainsCounter) != VK_SUCCESS)
	{
		return false;
	}

	// The monotonic clock needs no conversion, and a domain shared by the
	// stages needs one calibration rather than one per stage.
	for (const VkTimeDomainKHR preferred : {VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
		VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT, VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT})
	{
		const auto domain = std::find(domains.begin(), domains.begin() + properties.timeDomainCount, preferred);
		if (domain == domains.begin() + properties.timeDomainCount)
			continue;
		m_TimeDomain = preferred;
		m_TimeDomainID = domainIDs[domain - domains.begin()];
		return Calibrate();
	}
	return false;
}

bool CSwapChain::Calibrate()
{
	m_TimingsSinceCalibration = 0;
	if (m_TimeDomain == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR)
	{
		m_ReadyOffset = m_ShownOffset = 0;
		return true;
	}
	const auto offset = [this](const VkPresentStageFlagsEXT stage, int64_t& result)
	{
		VkSwapchainCalibratedTimestampInfoEXT swapchainInfo{};
		swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT;
		swapchainInfo.swapchain = m_SwapChain;
		swapchainInfo.presentStage = m_TimeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT ? stage : 0;
		swapchainInfo.timeDomainId = m_TimeDomainID;
		std::array<VkCalibratedTimestampInfoKHR, 2> infos{};
		infos[0].sType = infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
		infos[0].pNext = &swapchainInfo;
		infos[0].timeDomain = m_TimeDomain;
		infos[1].timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
		std::array<uint64_t, 2> timestamps{};
		uint64_t maxDeviation{0};
		if (vkGetCalibratedTimestampsKHR(m_Device->GetVkDevice(), static_cast<uint32_t>(infos.size()),
			infos.data(), timestamps.data(), &maxDeviation) != VK_SUCCESS)
		{
			return false;
		}
		result = static_cast<int64_t>(timestamps[0] - timestamps[1]);
		return true;
	};
	return offset(m_ShownStage, m_ShownOffset) && (m_ReadyStage == 0 || offset(m_ReadyStage, m_ReadyOffset));
}

void CSwapChain::ReadPresentTimings()
{
	for (VkPastPresentationTimingEXT& timing : m_Timings)
		timing.presentStageCount = 2;
	VkPastPresentationTimingInfoEXT info{};
	info.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT;
	info.swapchain = m_SwapChain;
	VkPastPresentationTimingPropertiesEXT properties{};
	properties.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT;
	properties.presentationTimingCount = static_cast<uint32_t>(m_Timings.size());
	properties.pPresentationTimings = m_Timings.data();
	const VkResult result{vkGetPastPresentationTimingEXT(m_Device->GetVkDevice(), &info, &properties)};
	if (result != VK_SUCCESS && result != VK_INCOMPLETE)
		return;

	// Recalibrate as the clocks slowly drift apart, and choose again when the
	// available domains change.
	constexpr uint32_t TIMINGS_PER_CALIBRATION{256};
	if (properties.timeDomainsCounter != m_TimeDomainsCounter)
		m_PresentTiming = ChooseTimeDomain();
	else if (m_TimingsSinceCalibration >= TIMINGS_PER_CALIBRATION)
		m_PresentTiming = Calibrate();
	m_Pacer.SetEveryPresentationReported(m_PresentTiming);

	for (uint32_t index = 0; index < properties.presentationTimingCount; ++index)
	{
		const VkPastPresentationTimingEXT& timing{m_Timings[index]};
		if (!timing.reportComplete)
			continue;
		m_PendingTimings -= std::min(m_PendingTimings, 1u);
		++m_TimingsSinceCalibration;
		uint64_t ready{0};
		uint64_t shown{0};
		for (uint32_t stage = 0; stage < timing.presentStageCount && timing.timeDomainId == m_TimeDomainID; ++stage)
		{
			const VkPresentStageTimeEXT& time{timing.pPresentStages[stage]};
			if (time.time == 0)
				continue;
			if (time.stage == m_ReadyStage)
				ready = time.time - m_ReadyOffset;
			else if (time.stage == m_ShownStage)
				shown = time.time - m_ShownOffset;
		}
		if (ready != 0)
			m_Pacer.OnFrameReady(timing.presentId, ready);
		m_Pacer.OnFrameDisplayed(timing.presentId, shown);
		m_ShownPresentID = std::max(m_ShownPresentID, timing.presentId);
	}

	if (properties.timingPropertiesCounter == m_TimingPropertiesCounter)
		return;
	VkSwapchainTimingPropertiesEXT timingProperties{};
	timingProperties.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT;
	uint64_t counter{0};
	if (vkGetSwapchainTimingPropertiesEXT(
		m_Device->GetVkDevice(), m_SwapChain, &timingProperties, &counter) != VK_SUCCESS)
	{
		return;
	}
	m_TimingPropertiesCounter = counter;
	// For a variable refresh the duration is the shortest, and an interval
	// of 0 means the display doesn't say which it has.
	if (timingProperties.refreshDuration == 0)
		return;
	const CFramePacer::RefreshMode mode{
		timingProperties.refreshInterval == std::numeric_limits<uint64_t>::max() ? CFramePacer::RefreshMode::VARIABLE :
		timingProperties.refreshInterval == 0 ? CFramePacer::RefreshMode::UNKNOWN : CFramePacer::RefreshMode::FIXED};
	m_Pacer.SetRefreshPeriod(timingProperties.refreshDuration, mode);
}

VkResult CSwapChain::WaitForPresent(const uint64_t id, const uint64_t timeout)
{
	if (m_PresentWait == PresentWait::V1)
		return vkWaitForPresentKHR(m_Device->GetVkDevice(), m_SwapChain, id, timeout);
	VkPresentWait2InfoKHR info{};
	info.sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR;
	info.presentId = id;
	info.timeout = timeout;
	return vkWaitForPresent2KHR(m_Device->GetVkDevice(), m_SwapChain, &info);
}

uint64_t CSwapChain::GetNextOverdueTime() const
{
	return m_ShownPresentID < m_PresentID ? m_Pacer.GetOverdueTime(m_ShownPresentID + 1) : 0;
}

bool CSwapChain::CheckOverduePresents()
{
	const uint64_t now{Now()};
	for (uint64_t overdue{GetNextOverdueTime()}; overdue != 0 && now > overdue; overdue = GetNextOverdueTime())
	{
		const uint64_t next{m_ShownPresentID + 1};
		const VkResult result{WaitForPresent(next, 0)};
		if (result == VK_TIMEOUT)
		{
			m_Pacer.OnFrameNotShownBy(next, now);
			return true;
		}
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
			m_IsValid = false;
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			return false;
		m_ShownPresentID = next;
	}
	return false;
}

void CSwapChain::WaitForPresents(const uint64_t id, const uint64_t until)
{
	while (m_ShownPresentID < m_PresentID && (id == 0 || m_ShownPresentID < id))
	{
		const uint64_t next{m_ShownPresentID + 1};
		// A present already shown has no known time; one shown while waiting
		// for it does.
		uint64_t shown{0};
		VkResult result{WaitForPresent(next, 0)};
		if (result == VK_TIMEOUT)
		{
			const uint64_t now{Now()};
			if (now >= until)
				return;
			result = WaitForPresent(next, until - now);
			shown = Now();
			if (result == VK_TIMEOUT)
				m_Pacer.OnFrameNotShownBy(next, shown);
		}
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
			m_IsValid = false;
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			return;
		m_ShownPresentID = next;
		if (!m_PresentTiming)
			m_Pacer.OnFrameDisplayed(next, shown);
	}
}

double CSwapChain::WaitForFrameStart()
{
	if (!m_IsValid || m_PresentWait == PresentWait::NONE)
		return 0.0;
	PROFILE3("WaitForFrameStart");
	if (m_PresentTiming)
	{
		ReadPresentTimings();
		CheckOverduePresents();
	}
	else
		WaitForPresents(0, 0);

	CFramePacer::SSchedule schedule{m_Pacer.Schedule(Now())};
	if (schedule.deadline != 0)
	{
		// Without present timing, waiting on presents until the start is what
		// times them.
		if (!m_PresentTiming)
			WaitForPresents(0, schedule.start);
		// Until the start, check each present as it falls overdue: a late one
		// has taken this frame's vblank, so the frame aims at a later one.
		for (uint64_t overdue{GetNextOverdueTime()}, last{0};
			m_PresentTiming && overdue != 0 && overdue != last && overdue < schedule.start;
			last = overdue, overdue = GetNextOverdueTime())
		{
			SleepUntil(overdue + 1);
			if (CheckOverduePresents())
				schedule = m_Pacer.Schedule(Now());
		}
		SleepUntil(schedule.start);
	}
	else if (m_PresentID >= 2)
	{
		// Until there's a vblank to aim at, keep at most one frame queued. A
		// present that is never shown, as in a hidden window, must not stop
		// the game; the swapchain's own limit then paces it.
		constexpr uint64_t TIMEOUT_NANOSECONDS{100'000'000};
		WaitForPresents(m_PresentID - 1, Now() + TIMEOUT_NANOSECONDS);
	}
	const uint64_t previousDeadline{std::exchange(m_Schedule, schedule).deadline};
	m_Pacer.OnFrameStarted(m_PresentID + 1, Now(), schedule);
	if (previousDeadline == 0 || schedule.deadline <= previousDeadline)
		return 0.0;
	return static_cast<double>(schedule.deadline - previousDeadline) / 1e9;
}

IFramebuffer* CSwapChain::GetCurrentBackbuffer(
	const AttachmentLoadOp colorAttachmentLoadOp,
	const AttachmentStoreOp colorAttachmentStoreOp,
	const AttachmentLoadOp depthStencilAttachmentLoadOp,
	const AttachmentStoreOp depthStencilAttachmentStoreOp)
{
	ENSURE(m_IsValid);
	ENSURE(m_CurrentImageIndex != std::numeric_limits<uint32_t>::max());
	SwapChainBackbuffer& swapChainBackbuffer =
		m_Backbuffers[m_CurrentImageIndex];
	const SwapChainBackbuffer::BackbufferKey key{
		colorAttachmentLoadOp, colorAttachmentStoreOp,
		depthStencilAttachmentLoadOp, depthStencilAttachmentStoreOp};
	auto it = swapChainBackbuffer.backbuffers.find(key);
	if (it == swapChainBackbuffer.backbuffers.end())
	{
		char nameBuffer[64];
		snprintf(nameBuffer, std::size(nameBuffer), "Backbuffer #%u", m_CurrentImageIndex);

		SColorAttachment colorAttachment{};
		colorAttachment.texture = m_Textures[m_CurrentImageIndex].get();
		colorAttachment.loadOp = colorAttachmentLoadOp;
		colorAttachment.storeOp = colorAttachmentStoreOp;

		SDepthStencilAttachment depthStencilAttachment{};
		depthStencilAttachment.texture = m_DepthTexture.get();
		depthStencilAttachment.loadOp = depthStencilAttachmentLoadOp;
		depthStencilAttachment.storeOp = depthStencilAttachmentStoreOp;

		it = swapChainBackbuffer.backbuffers.emplace(key, CFramebuffer::Create(
			m_Device, nameBuffer, &colorAttachment, &depthStencilAttachment)).first;
	}
	return it->second.get();
}

CTexture* CSwapChain::GetCurrentBackbufferTexture()
{
	ENSURE(m_CurrentImageIndex != std::numeric_limits<uint32_t>::max());
	return m_Textures[m_CurrentImageIndex].get();
}

CTexture* CSwapChain::GetOrCreateBackbufferReadbackTexture()
{
	ENSURE(m_IsValid);
	if (!m_BackbufferReadbackTexture)
	{
		CTexture* currentBackbufferTexture{GetCurrentBackbufferTexture()};
		m_BackbufferReadbackTexture = CTexture::CreateReadback(
			m_Device, "BackbufferReadback",
			currentBackbufferTexture->GetFormat(),
			currentBackbufferTexture->GetWidth(),
			currentBackbufferTexture->GetHeight());
	}
	return m_BackbufferReadbackTexture.get();
}

} // namespace Vulkan

} // namespace Backend

} // namespace Renderer
