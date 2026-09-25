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

#ifndef INCLUDED_RENDERER_BACKEND_IDEVICE
#define INCLUDED_RENDERER_BACKEND_IDEVICE

#include "graphics/Color.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/ITexture.h"

#include <cstdint>
#include <js/TypeDecls.h>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

class CShaderDefines;
class CStr;
namespace Renderer::Backend { class IComputePipelineState; }
namespace Renderer::Backend { class IDeviceCommandContext; }
namespace Renderer::Backend { class IFramebuffer; }
namespace Renderer::Backend { class IGraphicsPipelineState; }
namespace Renderer::Backend { class IShaderProgram; }
namespace Renderer::Backend { class ISwapChain; }
namespace Renderer::Backend { class IVertexInputLayout; }
namespace Renderer::Backend { enum class AttachmentLoadOp; }
namespace Renderer::Backend { enum class AttachmentStoreOp; }
namespace Renderer::Backend { enum class Backend; }
namespace Renderer::Backend { enum class Format; }
namespace Renderer::Backend { struct SColorAttachment; }
namespace Renderer::Backend { struct SComputePipelineStateDesc; }
namespace Renderer::Backend { struct SDepthStencilAttachment; }
namespace Renderer::Backend { struct SGraphicsPipelineStateDesc; }
namespace Renderer::Backend { struct SVertexAttributeFormat; }
namespace Renderer::Backend::Sampler { struct Desc; }
namespace Script { class Request; }

typedef struct SDL_Window SDL_Window;

namespace Renderer
{

namespace Backend
{

class IDevice
{
public:
	struct Capabilities
	{
		bool S3TC;
		bool computeShaders;
		bool debugLabels;
		bool debugScopedLabels;
		bool multisampling;
		bool anisotropicFiltering;
		uint32_t maxSampleCount;
		float maxAnisotropy;
		uint32_t maxTextureSize;
		bool instancing;
		bool storage;
		// Storage buffers can be read in the vertex stage.
		bool vertexStorage = false;
		bool timestamps;
		double timestampMultiplier;
	};

	/**
	 * It's a responsibility of a device owner to make sure (via WaitUntilIdle)
	 * that the device is available to be destroyed.
	 */
	virtual ~IDevice() {}

	virtual Backend GetBackend() const = 0;

	virtual const std::string& GetName() const = 0;
	virtual const std::string& GetVersion() const = 0;
	virtual const std::string& GetDriverInformation() const = 0;
	virtual const std::vector<std::string>& GetExtensions() const = 0;

	virtual void Report(const Script::Request& rq, JS::HandleValue settings) = 0;

	virtual std::unique_ptr<IDeviceCommandContext> CreateCommandContext() = 0;

	/**
	 * To be able to present something on a window it needs to create swapchain
	 * which provides framebuffer to output rendering result. Generally it's
	 * not allowed to have multiple swapchains for the same window.
	 *
	 * We use the provided surface size when the window is nullptr. It's used
	 * in Atlas when we don't have an SDL_Window.
	 *
	 * @return A valid swapchain if it was created successfully else nullptr.
	 */
	virtual std::unique_ptr<ISwapChain> CreateSwapChain(
		const char* name, SDL_Window* window,
		int surfaceDrawableWidth, int surfaceDrawableHeight, const bool vsync,
		std::unique_ptr<ISwapChain> oldSwapChain) = 0;

	/**
	 * Waits until all submitted work (via DeviceCommandContext::Flush) to
	 * backend is completed and it's safe to release a resource like SwapChain.
	 */
	virtual void WaitUntilIdle() = 0;

	/**
	 * Transfer the graphics context to or from the calling thread.
	 * Backends without thread-affine contexts need no handoff.
	 */
	virtual void BindToCurrentThread() {}
	virtual void UnbindFromCurrentThread() {}

	/**
	 * Creates a graphics pipeline state. It's a caller responsibility to
	 * guarantee a lifespan of IShaderProgram stored in the description.
	 */
	virtual std::unique_ptr<IGraphicsPipelineState> CreateGraphicsPipelineState(
		const SGraphicsPipelineStateDesc& pipelineStateDesc) = 0;

	/**
	 * Creates a compute pipeline state. It's a caller responsibility to
	 * guarantee a lifespan of IShaderProgram stored in the description.
	 */
	virtual std::unique_ptr<IComputePipelineState> CreateComputePipelineState(
		const SComputePipelineStateDesc& pipelineStateDesc) = 0;

	/**
	 * Creates a vertex input layout. It's recommended to use as few different
	 * layouts as posible.
	 */
	virtual std::unique_ptr<IVertexInputLayout> CreateVertexInputLayout(
		const std::span<const SVertexAttributeFormat> attributes) = 0;

	virtual std::unique_ptr<ITexture> CreateTexture(
		const char* name, const ITexture::Type type, const uint32_t usage,
		const Format format, const uint32_t width, const uint32_t height,
		const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount, const uint32_t sampleCount) = 0;

	virtual std::unique_ptr<ITexture> CreateTexture2D(
		const char* name, const uint32_t usage,
		const Format format, const uint32_t width, const uint32_t height,
		const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount = 1, const uint32_t sampleCount = 1) = 0;

	/**
	 * @see IFramebuffer
	 *
	 * The color attachment and the depth-stencil attachment should not be
	 * nullptr at the same time. There should not be many different clear
	 * colors along all color attachments for all framebuffers created for
	 * the device.
	 *
	 * @return A valid framebuffer if it was created successfully else nullptr.
	 */
	virtual std::unique_ptr<IFramebuffer> CreateFramebuffer(
		const char* name, SColorAttachment* colorAttachment,
		SDepthStencilAttachment* depthStencilAttachment) = 0;

	virtual std::unique_ptr<IBuffer> CreateBuffer(
		const char* name, const IBuffer::Type type, const uint32_t size, const uint32_t usage) = 0;

	virtual std::unique_ptr<IShaderProgram> CreateShaderProgram(
		const CStr& name, const CShaderDefines& defines) = 0;

	virtual bool IsTextureFormatSupported(const Format format) const = 0;

	virtual bool IsFramebufferFormatSupported(const Format format) const = 0;

	/**
	 * Returns the most suitable format for the usage. Returns
	 * Format::UNDEFINED if there is no such format.
	 */
	virtual Format GetPreferredDepthStencilFormat(
		const uint32_t usage, const bool depth, const bool stencil) const = 0;

	virtual uint32_t AllocateQuery() = 0;

	virtual void FreeQuery(const uint32_t handle) = 0;

	/**
	 * @see GetQueryResult
	 *
	 * It must be called only if the query was submitted via
	 * IDeviceCommandContext::Flush.
	 *
	 * @param handle Must be a valid handle of a query.
	 *
	 * @return True if a result for the query is available.
	 */
	virtual bool IsQueryResultAvailable(const uint32_t handle) const = 0;

	/**
	 * After a call of the function the query result becomes invalid.
	 *
	 * @param handle Must be a valid handle of a query.
	 *
	 * @return A result for the query. The result is undefined if the query isn't
	 * ready.
	 */
	virtual uint64_t GetQueryResult(const uint32_t handle) = 0;

	virtual const Capabilities& GetCapabilities() const = 0;

	/**
	 * Collects backend-specific statistics.
	 */
	struct StatisticsItem
	{
		std::string_view name;
		std::string_view unit;
		std::variant<float, uint32_t> value;

		// clang can't do emplace_back yet because of the aggregate type.
		StatisticsItem(
			std::string_view name, std::string_view unit,
			std::variant<float, uint32_t> value)
			: name(name), unit(unit), value(value)
		{
		}
	};
	using StatisticsVector = std::vector<StatisticsItem>;
	virtual void CollectStatistics(StatisticsVector& statistics) const = 0;
};

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_IDEVICE
