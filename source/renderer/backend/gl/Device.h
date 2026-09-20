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

#ifndef INCLUDED_RENDERER_BACKEND_GL_DEVICE
#define INCLUDED_RENDERER_BACKEND_GL_DEVICE

#include "lib/config2.h"
#include "ps/CStr.h"
#include "renderer/backend/Backend.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/IFramebuffer.h"
#include "renderer/backend/ITexture.h"
#include "renderer/backend/gl/DeviceForward.h"

#if CONFIG2_GLES
#include "external_libraries/opengles2_wrapper.h"
#else
#include <glad/gl.h>
#endif

#include <cstddef>
#include <cstdint>
#include <js/TypeDecls.h>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace Renderer::Backend::GL { class CDeviceCommandContext; }
namespace Renderer::Backend::GL { class CFramebuffer; }

typedef struct SDL_Window SDL_Window;
typedef void* SDL_GLContext;

namespace Renderer
{

namespace Backend
{

namespace GL
{

class CDevice final : public IDevice
{
public:
	~CDevice() override;

	/**
	 * Creates the GL device and the GL context for the window if it presents.
	 */
	static std::unique_ptr<IDevice> Create(SDL_Window* window);

	Backend GetBackend() const override { return Backend::GL; }

	const std::string& GetName() const override { return m_Name; }
	const std::string& GetVersion() const override { return m_Version; }
	const std::string& GetDriverInformation() const override { return m_DriverInformation; }
	const std::vector<std::string>& GetExtensions() const override { return m_Extensions; }

	void Report(const Script::Request& rq, JS::HandleValue settings) override;

	std::unique_ptr<IDeviceCommandContext> CreateCommandContext() override;

	std::unique_ptr<ISwapChain> CreateSwapChain(
		const char* name, SDL_Window* window,
		int surfaceDrawableWidth, int surfaceDrawableHeight,
		const bool vsync, std::unique_ptr<ISwapChain> oldSwapChain) override;

	void WaitUntilIdle() override;

	std::unique_ptr<IGraphicsPipelineState> CreateGraphicsPipelineState(
		const SGraphicsPipelineStateDesc& pipelineStateDesc) override;

	std::unique_ptr<IComputePipelineState> CreateComputePipelineState(
		const SComputePipelineStateDesc& pipelineStateDesc) override;

	std::unique_ptr<IVertexInputLayout> CreateVertexInputLayout(
		const std::span<const SVertexAttributeFormat> attributes) override;

	CDeviceCommandContext* GetActiveCommandContext() { return m_ActiveCommandContext; }

	std::unique_ptr<ITexture> CreateTexture(
		const char* name, const ITexture::Type type, const uint32_t usage,
		const Format format, const uint32_t width, const uint32_t height,
		const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount, const uint32_t sampleCount) override;

	std::unique_ptr<ITexture> CreateTexture2D(
		const char* name, const uint32_t usage,
		const Format format, const uint32_t width, const uint32_t height,
		const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount = 1, const uint32_t sampleCount = 1) override;

	std::unique_ptr<IFramebuffer> CreateFramebuffer(
		const char* name, SColorAttachment* colorAttachment,
		SDepthStencilAttachment* depthStencilAttachment) override;

	std::unique_ptr<IBuffer> CreateBuffer(
		const char* name, const IBuffer::Type type, const uint32_t size, const uint32_t usage) override;

	std::unique_ptr<IShaderProgram> CreateShaderProgram(
		const CStr& name, const CShaderDefines& defines) override;

	bool UseFramebufferInvalidating() const { return m_UseFramebufferInvalidating; }
	bool UseClientVertexArrays() const { return m_UseClientVertexArrays; }

	bool IsTextureFormatSupported(const Format format) const override;

	bool IsFramebufferFormatSupported(const Format format) const override;

	Format GetPreferredDepthStencilFormat(
		const uint32_t usage, const bool depth, const bool stencil) const override;

	uint32_t AllocateQuery() override;

	void FreeQuery(const uint32_t handle) override;

	bool IsQueryResultAvailable(const uint32_t handle) const override;

	uint64_t GetQueryResult(const uint32_t handle) override;

	void InsertTimestampQuery(const uint32_t handle);

	const Capabilities& GetCapabilities() const override { return m_Capabilities; }

	void CollectStatistics(StatisticsVector& statistics) const override;

private:
	CDevice();

	SDL_Window* m_Window = nullptr;
	SDL_GLContext m_Context = nullptr;

	std::string m_Name;
	std::string m_Version;
	std::string m_DriverInformation;
	std::vector<std::string> m_Extensions;

	// GL can have the only one command context at once.
	// TODO: remove as soon as we have no GL code outside backend, currently
	// it's used only as a helper for transition.
	CDeviceCommandContext* m_ActiveCommandContext = nullptr;

	bool m_UseFramebufferInvalidating = false;
	bool m_UseClientVertexArrays = false;

	struct Query
	{
		GLuint query{};
		bool occupied{};
	};
	std::vector<Query> m_Queries;

	Capabilities m_Capabilities{};

	bool m_R8Supported{false};
};

} // namespace GL

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_GL_DEVICE
