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

#include "Device.h"

#include "lib/alignment.h"
#include "lib/code_generation.h"
#include "lib/config2.h"
#include "lib/debug.h"
#include "lib/secure_crt.h"
#include "ps/CLogger.h"
#include "ps/ConfigDB.h"
#include "ps/Profile.h"
#include "ps/strings/StringBuilder.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/gl/Buffer.h"
#include "renderer/backend/gl/DeviceCommandContext.h"
#include "renderer/backend/gl/DeviceForward.h"
#include "renderer/backend/gl/Framebuffer.h"
#include "renderer/backend/gl/PipelineState.h"
#include "renderer/backend/gl/ShaderProgram.h"
#include "renderer/backend/gl/SwapChain.h"
#include "renderer/backend/gl/Texture.h"
#include "scriptinterface/Object.h"

#if CONFIG2_GLES
#include "external_libraries/opengles2_wrapper.h"
#else
#include <glad/gl.h>
#endif

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/algorithm/string/split.hpp>
#include <climits>
#include <cstring>
#include <iterator>
#include <js/RootingAPI.h>
#include <limits>
#include <utility>

namespace Renderer
{

namespace Backend
{

namespace GL
{

namespace
{

std::string GetNameImpl()
{
	// GL_VENDOR+GL_RENDERER are good enough here, so we don't use WMI to detect the cards.
	// On top of that WMI can cause crashes with Nvidia Optimus and some netbooks
	// see https://gitea.wildfiregames.com/0ad/0ad/issues/1952
	//     https://gitea.wildfiregames.com/0ad/0ad/issues/1575
	char cardName[128];
	const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
	const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	// Happens if called before GL initialization.
	if (!vendor || !renderer)
		return {};
	sprintf_s(cardName, std::size(cardName), "%s %s", vendor, renderer);

	// Remove crap from vendor names. (don't dare touch the model name -
	// it's too risky, there are too many different strings).
#define SHORTEN(what, charsToKeep) \
	if (!strncmp(cardName, what, std::size(what) - 1)) \
		memmove(cardName + charsToKeep, cardName + std::size(what) - 1, (strlen(cardName) - (std::size(what) - 1) + 1) * sizeof(char));
	SHORTEN("ATI Technologies Inc.", 3);
	SHORTEN("NVIDIA Corporation", 6);
	SHORTEN("S3 Graphics", 2);					// returned by EnumDisplayDevices
	SHORTEN("S3 Graphics, Incorporated", 2);	// returned by GL_VENDOR
#undef SHORTEN

	return cardName;
}

std::string GetVersionImpl()
{
	return reinterpret_cast<const char*>(glGetString(GL_VERSION));
}

std::string GetDriverInformationImpl()
{
	// Usually GL_VERSION contains both OpenGL and driver versions.
	return reinterpret_cast<const char*>(glGetString(GL_VERSION));
}

std::vector<std::string> GetExtensionsImpl()
{
	std::vector<std::string> extensions;
	const std::string exts{reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS))};
	boost::split(extensions, exts, boost::algorithm::is_space(), boost::token_compress_on);
	std::sort(extensions.begin(), extensions.end());
	return extensions;
}

void GLAD_API_PTR OnDebugMessage(
	GLenum source, GLenum type, GLuint id, GLenum severity,
	GLsizei /*length*/, const GLchar* message, const void* /*user_param*/)
{
	std::string debugSource = "unknown";
	std::string debugType = "unknown";
	std::string debugSeverity = "unknown";

	switch (source)
	{
	case GL_DEBUG_SOURCE_API:
		debugSource = "the API";
		break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
		debugSource = "the window system";
		break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER:
		debugSource = "the shader compiler";
		break;
	case GL_DEBUG_SOURCE_THIRD_PARTY:
		debugSource = "a third party";
		break;
	case GL_DEBUG_SOURCE_APPLICATION:
		debugSource = "the application";
		break;
	case GL_DEBUG_SOURCE_OTHER:
		debugSource = "somewhere";
		break;
	}

	switch (type)
	{
	case GL_DEBUG_TYPE_ERROR:
		debugType = "error";
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		debugType = "deprecated behaviour";
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		debugType = "undefined behaviour";
		break;
	case GL_DEBUG_TYPE_PORTABILITY:
		debugType = "portability";
		break;
	case GL_DEBUG_TYPE_PERFORMANCE:
		debugType = "performance";
		break;
	case GL_DEBUG_TYPE_OTHER:
		debugType = "other";
		break;
	case GL_DEBUG_TYPE_MARKER:
		debugType = "marker";
		break;
	case GL_DEBUG_TYPE_PUSH_GROUP:
		debugType = "push group";
		break;
	case GL_DEBUG_TYPE_POP_GROUP:
		debugType = "pop group";
		break;
	}

	switch (severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:
		debugSeverity = "high";
		break;
	case GL_DEBUG_SEVERITY_MEDIUM:
		debugSeverity = "medium";
		break;
	case GL_DEBUG_SEVERITY_LOW:
		debugSeverity = "low";
		break;
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		debugSeverity = "notification";
		break;
	}

	if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
	{
		debug_printf(
			"OpenGL | %s: %s source: %s id %u: %s\n", debugSeverity.c_str(), debugType.c_str(), debugSource.c_str(), id, message);
	}
	else
	{
		LOGWARNING(
			"OpenGL | %s: %s source: %s id %u: %s\n", debugSeverity.c_str(), debugType.c_str(), debugSource.c_str(), id, message);
	}
}

void ResetGLError()
{
	do {} while (glGetError() != GL_NO_ERROR);
}

template<typename ParameterType, size_t parameterCount, bool doQueryCounterBits = false>
void ReportParameter(
	const Script::Request& rq, JS::HandleValue settings,
	const GLenum parameter, const char* paremeterName)
{
	static_assert(parameterCount > 0);

	const char* errorString = "(error)";

	ParameterType values[parameterCount];

	if constexpr (std::is_same_v<ParameterType, GLint>)
	{
		std::fill(std::begin(values), std::end(values), -1);
		if constexpr (doQueryCounterBits)
			glGetQueryiv(parameter, GL_QUERY_COUNTER_BITS, values);
		else
			glGetIntegerv(parameter, values);
	}
	else if constexpr (std::is_same_v<ParameterType, GLfloat>)
	{
		std::fill(std::begin(values), std::end(values), std::numeric_limits<GLfloat>::quiet_NaN());
		glGetFloatv(parameter, values);
	}
	else if constexpr (std::is_same_v<ParameterType, const char*>)
	{
		std::fill(std::begin(values), std::end(values), "");
		// In the newer GL versions we have glGetStringi but currently we get
		// only one string.
		static_assert(parameterCount == 1);
		values[0] = reinterpret_cast<const char*>(glGetString(parameter));
		if (!values[0])
			values[0] = errorString;
	}
	else
	{
		debug_warn("Unsupported type");
	}

	const bool errorHappened{glGetError() != GL_NO_ERROR};
	ResetGLError();

	char buffer[1024];
	for (size_t index{0}; index < parameterCount; ++index)
	{
		PS::StringBuilder stringBuilder{{std::begin(buffer), std::end(buffer)}};
		stringBuilder.Append(paremeterName);
		if constexpr (parameterCount > 1)
		{
			stringBuilder.Append('[');
			stringBuilder.Append(index);
			stringBuilder.Append(']');
		}
		if (errorHappened)
			Script::SetProperty(rq, settings, stringBuilder.Str().data(), errorString);
		else
			Script::SetProperty(rq, settings, stringBuilder.Str().data(), values[index]);
	}
}

} // anonymous namespace

// static
std::unique_ptr<IDevice> CDevice::Create(SDL_Window* window)
{
	std::unique_ptr<CDevice> device(new CDevice());

	if (window)
	{
		// According to https://wiki.libsdl.org/SDL_CreateWindow we don't need to
		// call SDL_GL_LoadLibrary if we have a window with SDL_WINDOW_OPENGL,
		// because it'll be called internally for the first created window.
		device->m_Window = window;
		device->m_Context = SDL_GL_CreateContext(device->m_Window);
		if (!device->m_Context)
		{
			LOGERROR("SDL_GL_CreateContext failed: '%s'", SDL_GetError());
			return nullptr;
		}
	}

#if CONFIG2_GLES
	const int version{gladLoadGLES2(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))};
#else
	const int version{gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))};
#endif
	if (!version)
	{
		DEBUG_DISPLAY_FATAL_ERROR(L"GLAD failed to load GL");
		return nullptr;
	}

	device->m_Name = GetNameImpl();
	device->m_Version = GetVersionImpl();
	device->m_DriverInformation = GetDriverInformationImpl();
	device->m_Extensions = GetExtensionsImpl();

	LOGMESSAGE("GL device name: %s", device->m_Name.c_str());
	LOGMESSAGE("GL version: %s", device->m_Version.c_str());

#if CONFIG2_GLES
	const bool minimumRequiredGLVersionSupported{version >= GLAD_MAKE_VERSION(2, 1)};
#else
	const bool minimumRequiredGLVersionSupported{version >= GLAD_MAKE_VERSION(2, 0)};
#endif

	if (!minimumRequiredGLVersionSupported || !GLAD_GL_EXT_framebuffer_object)
	{
		// It doesn't make sense to continue working here, because we're not
		// able to display anything.
		DEBUG_DISPLAY_FATAL_ERROR(
			L"Your graphics card doesn't appear to be fully compatible with OpenGL shaders."
			L" The game does not support pre-shader graphics cards."
			L" You are advised to try installing newer drivers and/or upgrade your graphics card."
			L" For more information, please see http://www.wildfiregames.com/forum/index.php?showtopic=16734"
		);
	}

	// Set packing parameters for uploading and downloading data.
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// Some drivers might invalidate an incorrect surface which leads to artifacts.
	if (g_ConfigDB.Get("renderer.backend.gl.enableframebufferinvalidating", false))
	{
#if CONFIG2_GLES
		device->m_UseFramebufferInvalidating = GLAD_GL_EXT_discard_framebuffer;
#else
		device->m_UseFramebufferInvalidating = GLAD_GL_ARB_invalidate_subdata;
#endif
	}

	Capabilities& capabilities = device->m_Capabilities;
	capabilities.computeShaders = version >= GLAD_MAKE_VERSION(4, 3);
#if CONFIG2_GLES
	// Some GLES implementations have GL_EXT_texture_compression_dxt1
	// but that only supports DXT1 so we can't use it.
	capabilities.S3TC = GLAD_GL_EXT_texture_compression_s3tc;
#else
	// Note: we don't bother checking for GL_S3_s3tc - it is incompatible
	// and irrelevant (was never widespread).
	capabilities.S3TC = GLAD_GL_EXT_texture_compression_s3tc;
#endif
#if CONFIG2_GLES
	capabilities.multisampling = false;
	capabilities.maxSampleCount = 1;
#else
	capabilities.multisampling =
		version >= GLAD_MAKE_VERSION(3, 3) && GLAD_GL_ARB_texture_multisample;
	if (capabilities.multisampling)
	{
		// By default GL_MULTISAMPLE should be enabled, but enable it for buggy drivers.
		glEnable(GL_MULTISAMPLE);
		GLint maxSamples = 1;
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
		capabilities.maxSampleCount = maxSamples;
	}
#endif
	capabilities.anisotropicFiltering = GLAD_GL_EXT_texture_filter_anisotropic;
	if (capabilities.anisotropicFiltering)
	{
		GLfloat maxAnisotropy = 1.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
		capabilities.maxAnisotropy = maxAnisotropy;
	}
	GLint maxTextureSize = 1024;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	capabilities.maxTextureSize = maxTextureSize;

#if CONFIG2_GLES
	const bool hasDebug = GLAD_GL_KHR_debug;
#else
	const bool hasDebug = GLAD_GL_KHR_debug;
#endif
	if (hasDebug)
	{
#ifdef NDEBUG
		const bool enableDebugMessages{g_ConfigDB.Get("renderer.backend.debugmessages", false)};
		capabilities.debugLabels = g_ConfigDB.Get("renderer.backend.debuglabels", false);
		capabilities.debugScopedLabels = g_ConfigDB.Get("renderer.backend.debugscopedlabels", false);
#else
		const bool enableDebugMessages = true;
		capabilities.debugLabels = true;
		capabilities.debugScopedLabels = true;
#endif
		if (enableDebugMessages)
		{
			glEnable(GL_DEBUG_OUTPUT);
#if !CONFIG2_GLES
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#else
#warning GLES without GL_DEBUG_OUTPUT_SYNCHRONOUS might call the callback from different threads which might be unsafe.
#endif
			glDebugMessageCallback(OnDebugMessage, nullptr);

			// Filter out our own debug group messages
			const GLuint id = 0x0AD;
			glDebugMessageControl(
				GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_PUSH_GROUP, GL_DONT_CARE, 1, &id, GL_FALSE);
			glDebugMessageControl(
				GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_POP_GROUP, GL_DONT_CARE, 1, &id, GL_FALSE);
		}
	}

#if CONFIG2_GLES
	capabilities.instancing = false;
	capabilities.storage = false;
	capabilities.timestamps = false;
#else
	capabilities.instancing =
		(version >= GLAD_MAKE_VERSION(3, 3) ||
		(GLAD_GL_ARB_draw_instanced && GLAD_GL_ARB_instanced_arrays));
	GLint maxStorageBufferSize{0};
	if (GLAD_GL_ARB_shader_storage_buffer_object)
		glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxStorageBufferSize);
	// Storage buffers might not work correctly on some Mesa drivers or might have
	// decreased performance. We need to investigate it further but for now we
	// disable storage buffers on Mesa.
	const bool disableStorageForMesa{
		device->m_Name.find("Mesa") != std::string::npos
		|| device->m_DriverInformation.find("Mesa") != std::string::npos};
	capabilities.storage =
		capabilities.computeShaders && maxStorageBufferSize > 0
		&& static_cast<size_t>(maxStorageBufferSize) >= 128 * MiB
		&& !disableStorageForMesa
		&& GLAD_GL_ARB_uniform_buffer_object
		&& GLAD_GL_ARB_shader_storage_buffer_object
		&& GLAD_GL_ARB_half_float_vertex
		&& GLAD_GL_ARB_program_interface_query;
	capabilities.timestamps = GLAD_GL_ARB_timer_query;
	if (capabilities.timestamps)
		capabilities.timestampMultiplier = 1.0 / 1e9;
#endif

	device->m_R8Supported = version >= GLAD_MAKE_VERSION(3, 0);

	return device;
}

CDevice::CDevice() = default;

CDevice::~CDevice()
{
#if !CONFIG2_GLES
	for (Query& query : m_Queries)
	{
		ENSURE(!query.occupied);
		glDeleteQueries(1, &query.query);
	}
#endif

	if (m_Context)
		SDL_GL_DestroyContext(m_Context);
}

void CDevice::Report(const Script::Request& rq, JS::HandleValue settings)
{
	Script::SetProperty(rq, settings, "name", "gl");

	// We need to reset all previous errors because we don't call glGetError
	// by default.
	ResetGLError();

#define INTEGER(NAME) ReportParameter<GLint, 1>(rq, settings, GL_##NAME, "GL_" #NAME)
#define INTEGER2(NAME) ReportParameter<GLint, 2>(rq, settings, GL_##NAME, "GL_" #NAME)

#define BOOL(NAME) INTEGER(NAME)

#define FLOAT(NAME) ReportParameter<GLfloat, 1>(rq, settings, GL_##NAME, "GL_" #NAME)
#define FLOAT2(NAME) ReportParameter<GLfloat, 2>(rq, settings, GL_##NAME, "GL_" #NAME)

#define STRING(NAME) ReportParameter<const char*, 1>(rq, settings, GL_##NAME, "GL_" #NAME)

#define QUERY_COUNTER_BITS(NAME) ReportParameter<GLint, 1, true>(rq, settings, GL_##NAME, "GL_" #NAME ".GL_QUERY_COUNTER_BITS")

	// Core OpenGL 1.3:
	// (We don't bother checking extension strings for anything older than 1.3;
	// it'll just produce harmless warnings)
	STRING(VERSION);
	STRING(VENDOR);
	STRING(RENDERER);
	STRING(EXTENSIONS);

#if !CONFIG2_GLES
	INTEGER(MAX_CLIP_PLANES);
#endif
	INTEGER(SUBPIXEL_BITS);
#if !CONFIG2_GLES
	INTEGER(MAX_3D_TEXTURE_SIZE);
#endif
	INTEGER(MAX_TEXTURE_SIZE);
	INTEGER(MAX_CUBE_MAP_TEXTURE_SIZE);
	INTEGER2(MAX_VIEWPORT_DIMS);

#if !CONFIG2_GLES
	BOOL(RGBA_MODE);
	BOOL(INDEX_MODE);
	BOOL(DOUBLEBUFFER);
	BOOL(STEREO);
#endif

	FLOAT2(ALIASED_POINT_SIZE_RANGE);
	FLOAT2(ALIASED_LINE_WIDTH_RANGE);
#if !CONFIG2_GLES
	INTEGER(MAX_ELEMENTS_INDICES);
	INTEGER(MAX_ELEMENTS_VERTICES);
	INTEGER(MAX_TEXTURE_UNITS);
#endif
	INTEGER(SAMPLE_BUFFERS);
	INTEGER(SAMPLES);
	// TODO: compressed texture formats
	INTEGER(RED_BITS);
	INTEGER(GREEN_BITS);
	INTEGER(BLUE_BITS);
	INTEGER(ALPHA_BITS);
#if !CONFIG2_GLES
	INTEGER(INDEX_BITS);
#endif
	INTEGER(DEPTH_BITS);
	INTEGER(STENCIL_BITS);

#if !CONFIG2_GLES

	// Core OpenGL 2.1:

	FLOAT(MAX_TEXTURE_LOD_BIAS);

	QUERY_COUNTER_BITS(SAMPLES_PASSED);

	STRING(SHADING_LANGUAGE_VERSION);

	INTEGER(MAX_VERTEX_ATTRIBS);
	INTEGER(MAX_VERTEX_UNIFORM_COMPONENTS);
	INTEGER(MAX_VARYING_FLOATS);
	INTEGER(MAX_COMBINED_TEXTURE_IMAGE_UNITS);
	INTEGER(MAX_VERTEX_TEXTURE_IMAGE_UNITS);

	INTEGER(MAX_FRAGMENT_UNIFORM_COMPONENTS);

	INTEGER(MAX_TEXTURE_IMAGE_UNITS);
	INTEGER(MAX_TEXTURE_COORDS);

	INTEGER(MAX_DRAW_BUFFERS);

	// Core OpenGL 3.0:

	if (GLAD_GL_EXT_gpu_shader4)
	{
		INTEGER(MIN_PROGRAM_TEXEL_OFFSET_EXT); // no _EXT version of these in glext.h
		INTEGER(MAX_PROGRAM_TEXEL_OFFSET_EXT);
	}

	INTEGER(MAX_COLOR_ATTACHMENTS_EXT);
	INTEGER(MAX_RENDERBUFFER_SIZE_EXT);

	if (GLAD_GL_EXT_framebuffer_multisample)
	{
		INTEGER(MAX_SAMPLES_EXT);
	}

	if (GLAD_GL_EXT_texture_array)
	{
		INTEGER(MAX_ARRAY_TEXTURE_LAYERS_EXT);
	}

	// Other interesting extensions:

	if (GLAD_GL_ARB_timer_query)
	{
		QUERY_COUNTER_BITS(TIME_ELAPSED);
	}

	if (GLAD_GL_ARB_timer_query)
	{
		QUERY_COUNTER_BITS(TIMESTAMP);
	}

	if (GLAD_GL_EXT_texture_filter_anisotropic)
	{
		FLOAT(MAX_TEXTURE_MAX_ANISOTROPY_EXT);
	}

	if (GLAD_GL_ARB_texture_rectangle)
	{
		INTEGER(MAX_RECTANGLE_TEXTURE_SIZE_ARB);
	}

	if (GLAD_GL_ARB_geometry_shader4)
	{
		INTEGER(MAX_GEOMETRY_TEXTURE_IMAGE_UNITS_ARB);
		INTEGER(MAX_GEOMETRY_OUTPUT_VERTICES_ARB);
		INTEGER(MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS_ARB);
		INTEGER(MAX_GEOMETRY_UNIFORM_COMPONENTS_ARB);
		INTEGER(MAX_GEOMETRY_VARYING_COMPONENTS_ARB);
		INTEGER(MAX_VERTEX_VARYING_COMPONENTS_ARB);
	}

	if (GLAD_GL_ARB_uniform_buffer_object)
	{
		INTEGER(MAX_UNIFORM_BLOCK_SIZE);
		INTEGER(MAX_UNIFORM_BUFFER_BINDINGS);
	}

	if (GLAD_GL_ARB_shader_storage_buffer_object)
	{
		INTEGER(MAX_SHADER_STORAGE_BLOCK_SIZE);
		INTEGER(MAX_SHADER_STORAGE_BUFFER_BINDINGS);
	}

#else // CONFIG2_GLES

	// Core OpenGL ES 2.0:

	STRING(SHADING_LANGUAGE_VERSION);
	INTEGER(MAX_VERTEX_ATTRIBS);
	INTEGER(MAX_VERTEX_UNIFORM_VECTORS);
	INTEGER(MAX_VARYING_VECTORS);
	INTEGER(MAX_COMBINED_TEXTURE_IMAGE_UNITS);
	INTEGER(MAX_VERTEX_TEXTURE_IMAGE_UNITS);
	INTEGER(MAX_FRAGMENT_UNIFORM_VECTORS);
	INTEGER(MAX_TEXTURE_IMAGE_UNITS);
	INTEGER(MAX_RENDERBUFFER_SIZE);

#endif // CONFIG2_GLES
}

std::unique_ptr<IDeviceCommandContext> CDevice::CreateCommandContext()
{
	std::unique_ptr<CDeviceCommandContext> commandContet = CDeviceCommandContext::Create(this);
	m_ActiveCommandContext = commandContet.get();
	return commandContet;
}

std::unique_ptr<ISwapChain> CDevice::CreateSwapChain(
	[[maybe_unused]] const char* name, SDL_Window* window,
	int surfaceDrawableWidth, int surfaceDrawableHeight,
	const bool vsync, std::unique_ptr<ISwapChain> oldSwapChain)
{
	oldSwapChain.reset();
	ENSURE(window == m_Window);
	if (window)
		SDL_GetWindowSizeInPixels(window, &surfaceDrawableWidth, &surfaceDrawableHeight);
	return CSwapChain::Create(
		this, window, surfaceDrawableWidth, surfaceDrawableHeight, vsync);
}

void CDevice::WaitUntilIdle()
{
	// Technically we don't need to call glFinish since a driver ensures safety
	// itself. Though it might give a hint to a driver and it might reduce a
	// memory spike during recreating resources.
	glFinish();
}

std::unique_ptr<IGraphicsPipelineState> CDevice::CreateGraphicsPipelineState(
	const SGraphicsPipelineStateDesc& pipelineStateDesc)
{
	return CGraphicsPipelineState::Create(this, pipelineStateDesc);
}

std::unique_ptr<IComputePipelineState> CDevice::CreateComputePipelineState(
	const SComputePipelineStateDesc& pipelineStateDesc)
{
	return CComputePipelineState::Create(this, pipelineStateDesc);
}

std::unique_ptr<IVertexInputLayout> CDevice::CreateVertexInputLayout(
	const std::span<const SVertexAttributeFormat> attributes)
{
	return std::make_unique<CVertexInputLayout>(this, attributes);
}

std::unique_ptr<ITexture> CDevice::CreateTexture(
	const char* name, const ITexture::Type type, const uint32_t usage,
	const Format format, const uint32_t width, const uint32_t height,
	const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount, const uint32_t sampleCount)
{
	return CTexture::Create(this, name, type, usage,
		format, width, height, defaultSamplerDesc, MIPLevelCount, sampleCount);
}

std::unique_ptr<ITexture> CDevice::CreateTexture2D(
	const char* name, const uint32_t usage,
	const Format format, const uint32_t width, const uint32_t height,
	const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount, const uint32_t sampleCount)
{
	return CreateTexture(name, CTexture::Type::TEXTURE_2D, usage,
		format, width, height, defaultSamplerDesc, MIPLevelCount, sampleCount);
}

std::unique_ptr<IFramebuffer> CDevice::CreateFramebuffer(
	const char* name, SColorAttachment* colorAttachment,
	SDepthStencilAttachment* depthStencilAttachment)
{
	return CFramebuffer::Create(
		this, name, colorAttachment, depthStencilAttachment);
}

std::unique_ptr<IBuffer> CDevice::CreateBuffer(
	const char* name, const IBuffer::Type type, const uint32_t size, const uint32_t usage)
{
	return CBuffer::Create(this, name, type, size, usage);
}

std::unique_ptr<IShaderProgram> CDevice::CreateShaderProgram(
	const CStr& name, const CShaderDefines& defines)
{
	return CShaderProgram::Create(this, name, defines);
}

bool CDevice::IsTextureFormatSupported(const Format format) const
{
	bool supported = false;
	switch (format)
	{
	case Format::UNDEFINED:
		break;

	case Format::R8G8B8_UNORM:
	case Format::R8G8B8A8_UNORM:
	case Format::A8_UNORM:
	case Format::L8_UNORM:
		supported = true;
		break;

	case Format::R32_SFLOAT:
	case Format::R32G32_SFLOAT:
	case Format::R32G32B32_SFLOAT:
		break;

	case Format::D16_UNORM:
	case Format::D24_UNORM:
	case Format::D32_SFLOAT:
		supported = true;
		break;
	case Format::D24_UNORM_S8_UINT:
#if !CONFIG2_GLES
		supported = true;
#endif
		break;

	case Format::D32_SFLOAT_S8_UINT:
		break;

	case Format::BC1_RGB_UNORM:
	case Format::BC1_RGBA_UNORM:
	case Format::BC2_UNORM:
	case Format::BC3_UNORM:
		supported = m_Capabilities.S3TC;
		break;

#if !CONFIG2_GLES
	case Format::R16G16B16A16_SFLOAT:
	case Format::R32G32B32A32_SFLOAT:
		supported = GLAD_GL_ARB_texture_float;
		break;
#endif

	default:
		break;
	}
	return supported;
}

bool CDevice::IsFramebufferFormatSupported(const Format format) const
{
	bool supported = false;
	switch (format)
	{
	case Format::UNDEFINED:
		break;
#if !CONFIG2_GLES
	case Format::R8_UNORM:
		supported = m_R8Supported;
		break;
	case Format::R16G16B16A16_SFLOAT:
	case Format::R32G32B32A32_SFLOAT:
		supported = GLAD_GL_ARB_texture_float;
		break;
#endif
	case Format::R8G8B8A8_UNORM:
		supported = true;
		break;
	default:
		break;
	}
	return supported;
}

Format CDevice::GetPreferredDepthStencilFormat(
	const uint32_t /*usage*/, const bool depth, const bool stencil) const
{
	ENSURE(depth || stencil);
	if (stencil)
#if CONFIG2_GLES
		return Format::UNDEFINED;
#else
		return Format::D24_UNORM_S8_UINT;
#endif
	else
		return Format::D24_UNORM;
}

uint32_t CDevice::AllocateQuery()
{
	auto it = std::find_if(
		m_Queries.begin(), m_Queries.end(), [](const CDevice::Query& query)
		{
			return !query.occupied;
		});
	if (it == m_Queries.end())
	{
		m_Queries.emplace_back();
#if !CONFIG2_GLES
		glGenQueries(1, &m_Queries.back().query);
#endif
		it = prev(m_Queries.end());
	}
	it->occupied = true;
	return std::distance(m_Queries.begin(), it);
}

void CDevice::FreeQuery(const uint32_t handle)
{
	ENSURE(handle < m_Queries.size());
	ENSURE(m_Queries[handle].occupied);
	m_Queries[handle].occupied = false;
}

bool CDevice::IsQueryResultAvailable(const uint32_t handle) const
{
	ENSURE(handle < m_Queries.size());

	GLint available{};
#if !CONFIG2_GLES
	glGetQueryObjectiv(m_Queries[handle].query, GL_QUERY_RESULT_AVAILABLE, &available);
#endif
	return available;
}

uint64_t CDevice::GetQueryResult(const uint32_t handle)
{
	ENSURE(handle < m_Queries.size());
	// TODO: maybe we should check QUERY_COUNTER_BITS to ensure it's
	// high enough (but apparently it might trigger GL errors on ATI)

	GLuint64 queryTimestamp{};
#if !CONFIG2_GLES
	// Use the non-suffixed function here, as defined by GL_ARB_timer_query.
	glGetQueryObjectui64v(m_Queries[handle].query, GL_QUERY_RESULT, &queryTimestamp);
#endif
	return queryTimestamp;
}

void CDevice::InsertTimestampQuery(const uint32_t handle)
{
	ENSURE(handle < m_Queries.size());
#if !CONFIG2_GLES
	glQueryCounter(m_Queries[handle].query, GL_TIMESTAMP);
#endif
}

void CDevice::CollectStatistics(StatisticsVector& statistics) const
{
	statistics.emplace_back("Query count", "", static_cast<uint32_t>(m_Queries.size()));
}

std::unique_ptr<IDevice> CreateDevice(SDL_Window* window)
{
	return GL::CDevice::Create(window);
}

} // namespace GL

} // namespace Backend

} // namespace Renderer
