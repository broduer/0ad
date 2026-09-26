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

#ifndef INCLUDED_RENDERER_BACKEND_VULKAN_SWAPCHAIN
#define INCLUDED_RENDERER_BACKEND_VULKAN_SWAPCHAIN

#include "renderer/backend/FramePacer.h"
#include "renderer/backend/IFramebuffer.h"
#include "renderer/backend/ISwapChain.h"
#include "renderer/backend/vulkan/SubmitScheduler.h"

#include <cstddef>
#include <cstdint>
#include <glad/vulkan.h>
#include <limits>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace Renderer::Backend::Vulkan { class CDevice; }
namespace Renderer::Backend::Vulkan { class CFramebuffer; }
namespace Renderer::Backend::Vulkan { class CRingCommandContext; }
namespace Renderer::Backend::Vulkan { class CTexture; }

namespace Renderer
{

namespace Backend
{

namespace Vulkan
{

class CSwapChain final : public ISwapChain
{
public:
	~CSwapChain() override;

	IDevice* GetDevice() override;

	bool IsValid() const override { return m_IsValid; }

	bool AcquireNextBackbuffer() override;

	IFramebuffer* GetCurrentBackbuffer(
		const AttachmentLoadOp colorAttachmentLoadOp,
		const AttachmentStoreOp colorAttachmentStoreOp,
		const AttachmentLoadOp depthStencilAttachmentLoadOp,
		const AttachmentStoreOp depthStencilAttachmentStoreOp) override;

	void Present() override;

	double WaitForFrameStart() override;

	VkSwapchainKHR GetVkSwapchain() { return m_SwapChain; }

	bool AcquireNextImage();
	void SubmitCommandsAfterAcquireNextImage(
		CRingCommandContext& commandContext);
	void SubmitCommandsBeforePresent(
		CRingCommandContext& commandContext);
	void Present(VkSemaphore submitDone, VkQueue queue);

	CTexture* GetCurrentBackbufferTexture();

	CTexture* GetOrCreateBackbufferReadbackTexture();

	CTexture* GetDepthTexture() { return m_DepthTexture.get(); }

private:
	friend class CDevice;

	static std::unique_ptr<CSwapChain> Create(
		CDevice* device, CSubmitScheduler* submitScheduler,
		const uint32_t queueFamilyIndex, VkQueue queue,
		const char* name, VkSurfaceKHR surface,
		int surfaceDrawableWidth, int surfaceDrawableHeight,
		const bool vsync, std::unique_ptr<ISwapChain> oldSwapChain);

	CSwapChain();

	CDevice* m_Device = nullptr;
	CSubmitScheduler* m_SubmitScheduler{nullptr};
	VkQueue m_Queue{VK_NULL_HANDLE};

	bool m_IsValid = false;
	VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;

	uint32_t m_CurrentImageIndex = std::numeric_limits<uint32_t>::max();

	std::vector<VkImage> m_Images;
	std::vector<std::unique_ptr<CTexture>> m_Textures;
	std::unique_ptr<CTexture> m_DepthTexture;
	VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;

	uint32_t m_FrameID{0};

	// With vsync, presents carry increasing IDs to be waited for, by
	// VK_KHR_present_wait or VK_KHR_present_wait2. Where the surface supports
	// it, VK_EXT_present_timing also times and schedules them.
	enum class PresentWait
	{
		NONE,
		V1,
		V2
	};
	PresentWait m_PresentWait{PresentWait::NONE};
	bool m_PresentTiming{false};
	uint64_t m_PresentID{0};
	// The newest present known to be shown.
	uint64_t m_ShownPresentID{0};

	// Present timing's time domain and the two stages it times: ready, when the
	// GPU is done with a frame, and shown, when the display takes it. Each
	// offset is how far the domain's clock is ahead of the monotonic clock.
	VkTimeDomainKHR m_TimeDomain{VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR};
	uint64_t m_TimeDomainID{0};
	uint64_t m_TimeDomainsCounter{0};
	VkPresentStageFlagsEXT m_ReadyStage{0};
	VkPresentStageFlagsEXT m_ShownStage{0};
	int64_t m_ReadyOffset{0};
	int64_t m_ShownOffset{0};
	uint32_t m_TimingsSinceCalibration{0};
	uint64_t m_TimingPropertiesCounter{std::numeric_limits<uint64_t>::max()};
	uint32_t m_PendingTimings{0};
	std::vector<VkPastPresentationTimingEXT> m_Timings;
	std::vector<VkPresentStageTimeEXT> m_StageTimes;

	CFramePacer m_Pacer;
	// The schedule of the frame being rendered, all 0 when unpaced.
	CFramePacer::SSchedule m_Schedule;

	void SetUpPresentTiming(const VkPresentTimingSurfaceCapabilitiesEXT& capabilities);
	bool ChooseTimeDomain();
	bool Calibrate();
	void ReadPresentTimings();
	/**
	 * Ask present wait about overdue presents, which it answers before their
	 * timing reports arrive. Report the first not yet shown to the pacer and
	 * return whether there was one.
	 */
	bool CheckOverduePresents();
	// Return when the oldest present not known to be shown falls overdue, or 0.
	uint64_t GetNextOverdueTime() const;
	VkResult WaitForPresent(const uint64_t id, const uint64_t timeout);
	/**
	 * Wait until present @p id is shown, or every present when @p id is 0, but
	 * not past @p until. Report each present shown meanwhile to the pacer.
	 */
	void WaitForPresents(const uint64_t id, const uint64_t until);

	// We can't reuse the same acquire semaphore immediately after present
	// because it might still be processing on GPU as vkQueuePresentKHR doesn't
	// have to be blocking.
	// We need to wait for the image on GPU to draw to it.
	struct FrameObject
	{
		VkSemaphore acquireImageSemaphore{VK_NULL_HANDLE};
		// We need to know when we can reuse the semaphore.
		CSubmitScheduler::SubmitHandle submitHandle{CSubmitScheduler::INVALID_SUBMIT_HANDLE};
	};
	std::vector<FrameObject> m_FrameObjects;

	// The number of submit semaphores should be equal to the number of images
	// in the swapchain. We could use NUMBER_OF_FRAMES_IN_FLIGHT of objects but
	// it might be not safe. Since vkQueuePresentKHR doesn't provide a way to
	// tell that a semaphore was signaled.
	//
	// A possible situation, list of acquired indices:
	//  0, 1, 2, 0, 1, 0, 1
	//                     ^
	// In theory in the end we might still have a semaphore in use for the
	// 2nd swapchain image.
	//
	// See also:
	//  https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
	// We need to present only after all submit work is done.
	std::vector<VkSemaphore> m_SubmitSemaphores;

	struct SwapChainBackbuffer
	{
		using BackbufferKey = std::tuple<
			AttachmentLoadOp, AttachmentStoreOp,
			AttachmentLoadOp, AttachmentStoreOp>;
		struct BackbufferKeyHash
		{
			size_t operator()(const BackbufferKey& key) const;
		};
		std::unordered_map<
			BackbufferKey, std::unique_ptr<CFramebuffer>, BackbufferKeyHash> backbuffers;

		SwapChainBackbuffer();

		SwapChainBackbuffer(const SwapChainBackbuffer&) = delete;
		SwapChainBackbuffer& operator=(const SwapChainBackbuffer&) = delete;

		SwapChainBackbuffer(SwapChainBackbuffer&& other);
		SwapChainBackbuffer& operator=(SwapChainBackbuffer&& other);
	};
	std::vector<SwapChainBackbuffer> m_Backbuffers;

	std::unique_ptr<CTexture> m_BackbufferReadbackTexture;

	std::unique_ptr<CRingCommandContext> m_AcquireCommandContext;
	std::unique_ptr<CRingCommandContext> m_PresentCommandContext;

	bool m_DebugWaitIdleBeforeAcquire = false;
	bool m_DebugWaitIdleBeforePresent = false;
	bool m_DebugWaitIdleAfterPresent = false;
};

} // namespace Vulkan

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_VULKAN_SWAPCHAIN
