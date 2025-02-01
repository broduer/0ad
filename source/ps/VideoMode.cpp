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

#include "VideoMode.h"

#include "graphics/Camera.h"
#include "graphics/GameView.h"
#include "graphics/ShaderDefines.h"
#include "graphics/ShaderProgram.h"
#include "gui/GUIManager.h"
#include "lib/config2.h"
#include "lib/debug.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "lib/status.h"
#include "lib/sysdep/os.h"
#include "lib/tex/tex.h"
#include "lib/types.h"
#include "ps/CConsole.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/Game.h"
#include "ps/GameSetup/Config.h"
#include "ps/Hotkey.h"
#include "ps/Pyrogenesis.h"
#include "renderer/Renderer.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/ISwapChain.h"
#include "renderer/backend/dummy/DeviceForward.h"
#include "renderer/backend/gl/DeviceForward.h"
#include "renderer/backend/vulkan/DeviceForward.h"
#include "scriptinterface/Interface.h"

#if OS_MACOSX
#include "ps/DllLoader.h"
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

#if OS_MACOSX
#include <SDL3/SDL_vulkan.h>
#include <stdlib.h>
#endif

using namespace std::literals;

namespace
{

int DEFAULT_WINDOW_W = 1024;
int DEFAULT_WINDOW_H = 768;

const wchar_t DEFAULT_CURSOR_NAME[] = L"default-arrow";

Renderer::Backend::Backend GetFallbackBackend(const Renderer::Backend::Backend backend)
{
	Renderer::Backend::Backend fallback = Renderer::Backend::Backend::DUMMY;
	// We use a switch instead of a list to have compile-time checks for missed
	// values and because a linear priority list doesn't work for general case.
	switch (backend)
	{
	case Renderer::Backend::Backend::GL:
		fallback = Renderer::Backend::Backend::DUMMY;
		break;
	case Renderer::Backend::Backend::DUMMY:
		break;
	case Renderer::Backend::Backend::VULKAN:
		fallback = Renderer::Backend::Backend::GL;
		break;
	}
	return fallback;
}

std::string_view GetBackendName(const Renderer::Backend::Backend backend)
{
	std::string_view name{"Unknown"};
	switch (backend)
	{
	case Renderer::Backend::Backend::GL:
		name = "GL";
		break;
	case Renderer::Backend::Backend::DUMMY:
		name = "Dummy";
		break;
	case Renderer::Backend::Backend::VULKAN:
		name = "Vulkan";
		break;
	}
	return name;
}

} // anonymous namespace

#if OS_WIN
// We can't include wutil directly because GL headers conflict with Windows
// until we use a proper GL loader.
extern void wutil_SetAppWindow(SDL_Window* window);

// After a proper HiDPI integration we should switch to manifest until
// SDL has an implemented HiDPI on Windows.
extern void wutil_EnableHiDPIOnWindows();
#endif

CVideoMode g_VideoMode;

class CVideoMode::CCursor
{
public:
	enum class CursorBackend
	{
		SDL,
		SYSTEM
	};

	CCursor();
	~CCursor();

	void SetCursor(const CStrW& name);
	void ResetCursor();

private:
	CursorBackend m_CursorBackend = CursorBackend::SYSTEM;
	SDL_Surface* m_CursorSurface = nullptr;
	SDL_Cursor* m_Cursor = nullptr;
	CStrW m_CursorName;
};

CVideoMode::CCursor::CCursor()
{
	if (g_ConfigDB.Get("cursorbackend", std::string{}) == "sdl")
		m_CursorBackend = CursorBackend::SDL;
	else
		m_CursorBackend = CursorBackend::SYSTEM;

	ResetCursor();
}

CVideoMode::CCursor::~CCursor()
{
	if (m_Cursor)
		SDL_DestroyCursor(m_Cursor);
	if (m_CursorSurface)
		SDL_DestroySurface(m_CursorSurface);
}

void CVideoMode::CCursor::SetCursor(const CStrW& name)
{
	if (m_CursorBackend == CursorBackend::SYSTEM || m_CursorName == name)
		return;
	m_CursorName = name;

	if (m_Cursor)
		SDL_DestroyCursor(m_Cursor);
	if (m_CursorSurface)
		SDL_DestroySurface(m_CursorSurface);

	if (name.empty())
	{
		SDL_HideCursor();
		return;
	}

	const VfsPath pathBaseName(VfsPath(L"art/textures/cursors") / name);

	// Read pixel offset of the cursor's hotspot [the bit of it that's
	// drawn at (g_mouse_x,g_mouse_y)] from file.
	int hotspotX = 0, hotspotY = 0;
	{
		const VfsPath pathHotspotName = pathBaseName.ChangeExtension(L".txt");
		std::shared_ptr<u8> buffer;
		size_t size;
		if (g_VFS->LoadFile(pathHotspotName, buffer, size) != INFO::OK)
		{
			LOGERROR("Can't load hotspot for cursor: %s", pathHotspotName.string8().c_str());
			return;
		}
		std::wstringstream s(std::wstring(reinterpret_cast<const wchar_t*>(buffer.get()), size));
		s >> hotspotX >> hotspotY;
	}

	const VfsPath pathImageName = pathBaseName.ChangeExtension(L".png");

	std::shared_ptr<u8> file;
	size_t fileSize;
	if (g_VFS->LoadFile(pathImageName, file, fileSize) != INFO::OK)
	{
		LOGERROR("Can't load image for cursor: %s", pathImageName.string8().c_str());
		return;
	}

	Tex t;
	if (t.decode(file, fileSize) != INFO::OK)
	{
		LOGERROR("Can't decode image for cursor");
		return;
	}

	// Convert to required BGRA format.
	const size_t flags = (t.m_Flags | TEX_BGR) & ~TEX_DXT;
	if (t.transform_to(flags) != INFO::OK)
	{
		LOGERROR("Can't transform image for cursor");
		return;
	}
	void* imageBGRA = t.get_data();
	if (!imageBGRA)
	{
		LOGERROR("Transformed image is empty for cursor");
		return;
	}

	m_CursorSurface = SDL_CreateSurfaceFrom(
		static_cast<int>(t.m_Width), static_cast<int>(t.m_Height),
		SDL_GetPixelFormatForMasks(32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000),
		imageBGRA, static_cast<int>(t.m_Width * 4));
	if (!m_CursorSurface)
	{
		LOGERROR("Can't create surface for cursor: %s", SDL_GetError());
		return;
	}
	const float scale = g_VideoMode.GetScale();
	if (scale != 1.0)
	{
		SDL_Surface* scaledSurface = SDL_CreateSurface(
			m_CursorSurface->w * scale, m_CursorSurface->h * scale,
			SDL_GetPixelFormatForMasks(32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000));
		if (!scaledSurface)
		{
			LOGERROR("Can't create scaled surface forcursor: %s", SDL_GetError());
			return;
		}
		if (!SDL_BlitSurfaceScaled(m_CursorSurface, nullptr, scaledSurface, nullptr, SDL_SCALEMODE_LINEAR))
			return;
		SDL_DestroySurface(m_CursorSurface);
		m_CursorSurface = scaledSurface;
	}
	m_Cursor = SDL_CreateColorCursor(m_CursorSurface, hotspotX, hotspotY);
	if (!m_Cursor)
	{
		LOGERROR("Can't create cursor: %s", SDL_GetError());
		return;
	}

	SDL_SetCursor(m_Cursor);
}

void CVideoMode::CCursor::ResetCursor()
{
	SetCursor(DEFAULT_CURSOR_NAME);
}

Input::Reaction CVideoMode::InputHandler::operator()(const SDL_Event& ev)
{

	switch (ev.type)
	{
	case SDL_HOTKEYPRESS:
	{
		if (static_cast<const char*>(ev.user.data1) == "mousegrabtoggle"sv)
		{
			SDL_Window* const window{videoMode.GetWindow()};
			SDL_SetWindowMouseGrab(window, !SDL_GetWindowMouseGrab(window));
			return Input::Reaction::HANDLED;
		}
		break;
	}
	case SDL_EVENT_WINDOW_RESIZED:
		videoMode.m_ResizedW = ev.window.data1;
		videoMode.m_ResizedH = ev.window.data2;
		break;
	}

	return Input::Reaction::PASS;
}

CVideoMode::CVideoMode() = default;

CVideoMode::~CVideoMode() = default;

void CVideoMode::ReadConfig()
{
	m_ConfigFullscreen = !g_ConfigDB.Get("windowed", !m_ConfigFullscreen);

	m_Scale = g_ConfigDB.Get("gui.scale", m_Scale);

	m_ConfigW = g_ConfigDB.Get("xres", m_ConfigW);
	m_ConfigH = g_ConfigDB.Get("yres", m_ConfigH);
	m_ConfigBPP = g_ConfigDB.Get("bpp", m_ConfigBPP);
	m_ConfigDisplay = g_ConfigDB.Get("display", m_ConfigDisplay);
	m_ConfigEnableHiDPI = g_ConfigDB.Get("hidpi", m_ConfigEnableHiDPI);
	m_ConfigVSync = g_ConfigDB.Get("vsync", m_ConfigVSync);
	m_ConfigBorderlessWindow = g_ConfigDB.Get("borderless.window", m_ConfigBorderlessWindow);
	m_ConfigMouseGrabInFullscreen = g_ConfigDB.Get("window.mousegrabinfullscreen", m_ConfigMouseGrabInFullscreen);
	m_ConfigMouseGrabInWindowMode = g_ConfigDB.Get("window.mousegrabinwindowmode", m_ConfigMouseGrabInWindowMode);

	const std::string rendererBackend{g_ConfigDB.Get("rendererbackend", std::string{})};
	if (rendererBackend == "dummy")
		m_Backend = Renderer::Backend::Backend::DUMMY;
	else if (rendererBackend == "vulkan")
		m_Backend = Renderer::Backend::Backend::VULKAN;
	else
		m_Backend = Renderer::Backend::Backend::GL;

#if OS_WIN
	if (m_ConfigEnableHiDPI)
		wutil_EnableHiDPIOnWindows();
#endif
}

void CVideoMode::InitGLContext()
{
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	if (g_ConfigDB.Get("renderer.backend.debugcontext", false))
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

	if (g_ConfigDB.Get("forceglversion", false))
	{
		const std::string forceGLProfile = g_ConfigDB.Get("forceglprofile", "compatibility"s);
		const int forceGLMajorVersion = g_ConfigDB.Get("forceglmajorversion", 3);
		const int forceGLMinorVersion = g_ConfigDB.Get("forceglminorversion", 0);

		int profile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
		if (forceGLProfile == "es")
			profile = SDL_GL_CONTEXT_PROFILE_ES;
		else if (forceGLProfile == "core")
			profile = SDL_GL_CONTEXT_PROFILE_CORE;
		else if (forceGLProfile != "compatibility")
			LOGWARNING("Unknown force GL profile '%s', compatibility profile is used", forceGLProfile.c_str());

		if (forceGLMajorVersion < 1 || forceGLMinorVersion < 0)
		{
			LOGERROR("Unsupported force GL version: %d.%d", forceGLMajorVersion, forceGLMinorVersion);
		}
		else
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, forceGLMajorVersion);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, forceGLMinorVersion);
		}
	}
	else
	{
#if CONFIG2_GLES
		// Require GLES 2.0
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
		// Some macOS and MESA drivers might not create a context even if they can
		// with the core profile. So disable it for a while until we can guarantee
		// its creation.
#if OS_WIN
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
	}
}

bool CVideoMode::InitWindow(int windowW, int windowH)
{

	const bool isGLBackend = m_Backend == Renderer::Backend::Backend::GL;

	if (isGLBackend)
		InitGLContext();

#if OS_MACOSX
	if (m_Backend == Renderer::Backend::Backend::VULKAN)
	{
		// MoltenVK - enable full component swizzling support.
		setenv("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE", "1", 1);
		CStr fullPathToVulkanLibrary = DllLoader::GenerateFilename("MoltenVK", "", ".dylib");
		// MoltenVK - only print warnings and errors.
		setenv("MVK_CONFIG_LOG_LEVEL", "2", 1);
		if (!SDL_Vulkan_LoadLibrary(fullPathToVulkanLibrary.c_str()))
		{
			LOGWARNING("Failed to load %s: %s", fullPathToVulkanLibrary.c_str(), SDL_GetError());
			DowngradeBackendSettingAfterCreationFailure();
			return InitWindow(windowW, windowH);
		}
		else
			LOGMESSAGE("Loaded %s.", fullPathToVulkanLibrary.c_str());
	}
#endif

	// Note: these flags only take affect in SDL_CreateWindow
	SDL_WindowFlags flags{SDL_WINDOW_RESIZABLE};
	if (m_ConfigBorderlessWindow)
		flags |= SDL_WINDOW_BORDERLESS;
	if (m_ConfigFullscreen)
	{
		flags |= SDL_WINDOW_FULLSCREEN;
	}
	if (m_ConfigEnableHiDPI)
		flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
	if (isGLBackend)
		flags |= SDL_WINDOW_OPENGL;
	else if (m_Backend == Renderer::Backend::Backend::VULKAN)
		flags |= SDL_WINDOW_VULKAN;

	// Try create the window.
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, main_window_name);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(m_ConfigDisplay));
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(m_ConfigDisplay));
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, windowW);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, windowH);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, flags);
	m_Window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);

	if (!m_Window)
	{

		// SDL might fail to create a window in case of missing a Vulkan driver.
		if (m_Backend == Renderer::Backend::Backend::VULKAN)
		{
			LOGWARNING("Failed to create a Vulkan window: %s", SDL_GetError());
			DowngradeBackendSettingAfterCreationFailure();
			return InitWindow(windowW, windowH);
		}

		if (isGLBackend)
		{
			int depthSize = 24;
			SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthSize);
			if (depthSize > 16)
			{
				// Fall back to a smaller depth buffer
				// (The rendering may be ugly but this helps when running in VMware)
				SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

				return InitWindow(windowW, windowH);
			}
		}

		LOGERROR("InitWindow failed in SDL_CreateWindow: (\"%s\")", SDL_GetError());
		return false;
	}

#if OS_WIN
	// We need to set the window for an error dialog.
	wutil_SetAppWindow(m_Window);
#endif

	if (!TryCreateBackendDevice(m_Window))
	{
		DowngradeBackendSettingAfterCreationFailure();
		SDL_DestroyWindow(m_Window);
		m_Window = nullptr;
		return InitWindow(windowW, windowH);
	}

	return true;
}

bool CVideoMode::InitSDL()
{
	ENSURE(!m_IsInitialised);

	ReadConfig();

	const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(m_ConfigDisplay);
	if (mode)
	{
		m_PreferredW = mode->w;
		m_PreferredH = mode->h;
		m_PreferredBPP = SDL_BITSPERPIXEL(mode->format);
		m_PreferredFreq = mode->refresh_rate;
	}

	int w = m_ConfigW;
	int h = m_ConfigH;

	// If no size determined, default to something sensible
	if (w == 0 || h == 0)
	{
		w = DEFAULT_WINDOW_W;
		h = DEFAULT_WINDOW_H;
	}

	if (m_PreferredW)
		w = std::min(w, m_PreferredW);
	if (m_PreferredH)
		h = std::min(h, m_PreferredH);

	if (!m_Window)
	{
		InitWindow(w, h);
	}

	SDL_GetWindowSize(m_Window, &m_CurrentW, &m_CurrentH);
	UpdateRenderer(m_CurrentW, m_CurrentH);

	// Work around a bug in the proprietary Linux ATI driver (at least versions 8.16.20 and 8.14.13).
	// The driver appears to register its own atexit hook on context creation.
	// If this atexit hook is called before SDL_Quit destroys the OpenGL context,
	// some kind of double-free problem causes a crash and lockup in the driver.
	// Calling SDL_Quit twice appears to be harmless, though, and avoids the problem
	// by destroying the context *before* the driver's atexit hook is called.
	// (Note that atexit hooks are guaranteed to be called in reverse order of their registration.)
	if (m_Backend == Renderer::Backend::Backend::GL)
		atexit(SDL_Quit);
	// End work around.

	m_IsInitialised = true;

	SetWindowIcon();

	m_Cursor = std::make_unique<CCursor>();

	return true;
}

bool CVideoMode::InitNonSDL()
{
	ENSURE(!m_IsInitialised);

	ReadConfig();

	m_IsInitialised = true;

	return true;
}

void CVideoMode::Shutdown()
{
	ENSURE(m_IsInitialised);

	m_Cursor.reset();

	m_IsInitialised = false;

	if (m_BackendDevice)
	{
		// We need to wait before we can destroy the device.
		m_BackendDevice->WaitUntilIdle();
	}

	m_SwapChain.reset();
	m_BackendDevice.reset();
	if (m_Window)
	{
		SDL_DestroyWindow(m_Window);
		m_Window = nullptr;
	}
}

bool CVideoMode::CreateBackendDevice(const bool createSDLContext)
{
	if (!createSDLContext && m_Backend == Renderer::Backend::Backend::VULKAN)
		m_Backend = Renderer::Backend::Backend::GL;
	SDL_Window* window = createSDLContext ? m_Window : nullptr;
	while (m_Backend != Renderer::Backend::Backend::DUMMY)
	{
		if (TryCreateBackendDevice(window))
			return true;
		DowngradeBackendSettingAfterCreationFailure();
	}
	return TryCreateBackendDevice(window);
}

bool CVideoMode::TryCreateBackendDevice(SDL_Window* window)
{
	switch (m_Backend)
	{
	case Renderer::Backend::Backend::GL:
		m_BackendDevice = Renderer::Backend::GL::CreateDevice(window);
		break;
	case Renderer::Backend::Backend::DUMMY:
		m_BackendDevice = Renderer::Backend::Dummy::CreateDevice(window);
		ENSURE(m_BackendDevice);
		break;
	case Renderer::Backend::Backend::VULKAN:
		m_BackendDevice = Renderer::Backend::Vulkan::CreateDevice(window);
		// HACK: the repository doesn't have prebuilt SPIR-V shaders. So some
		// users might get an error in case of missing shaders.
		if (m_BackendDevice)
		{
			// We must not use CShaderManager here to avoid caching.
			std::unique_ptr<Renderer::Backend::IShaderProgram> shaderProgram =
				m_BackendDevice->CreateShaderProgram("spirv/canvas2d", CShaderDefines{});
			if (!shaderProgram)
			{
				m_BackendDevice.reset();
			}
		}
		break;
	}
	// We don't need to wait because we don't have any swapchain in use.
	RecreateSwapChain();
	if (m_BackendDevice && (!m_SwapChain || !m_SwapChain->IsValid()))
	{
		// Currently we assume that we should have a valid swapchain on the device
		// creation.
		m_BackendDevice.reset();
	}
	return static_cast<bool>(m_BackendDevice);
}

void CVideoMode::DowngradeBackendSettingAfterCreationFailure()
{
	const Renderer::Backend::Backend fallback = GetFallbackBackend(m_Backend);
	LOGERROR("Unable to create device for %s backend, switching to %s.",
		GetBackendName(m_Backend), GetBackendName(fallback));
	m_Backend = fallback;
}

void CVideoMode::RecreateSwapChain()
{
	if (m_BackendDevice)
	{
		char nameBuffer[64];
		snprintf(nameBuffer, std::size(nameBuffer), "SwapChain: %dx%d", m_CurrentW, m_CurrentH);
		m_SwapChain = m_BackendDevice->CreateSwapChain(
			nameBuffer, m_Window, m_CurrentW, m_CurrentH, m_ConfigVSync, std::move(m_SwapChain));
	}
}

Renderer::Backend::ISwapChain* CVideoMode::GetOrCreateSwapChain()
{
#if !OS_MACOSX
	const bool vsync{g_ConfigDB.Get("vsync", false)};
	const bool vsyncChanged{m_ConfigVSync != vsync};
	if (vsyncChanged)
		m_ConfigVSync = vsync;
#else
	// Currently there is a bug in MoltenVK which doesn't allow to recreate
	// a swapchain with the same size (it makes it 1x1):
	//  https://github.com/KhronosGroup/MoltenVK/pull/2722
	const bool vsyncChanged{false};
#endif
	const bool shouldRecreate{
		!m_SwapChain || !m_SwapChain->IsValid() || vsyncChanged};
	if (shouldRecreate)
	{
		if (m_SwapChain)
		{
			// We need to wait until we can destroy the swapchain because it
			// might be in use.
			m_BackendDevice->WaitUntilIdle();
		}
		RecreateSwapChain();
	}
	return m_SwapChain.get();
}

bool CVideoMode::OnceAFrameWork()
{
	ENSURE(m_IsInitialised);

	// Ignore if the size hasn't changed
	if ((m_ResizedH == 0 && m_ResizedW == 0) || (m_ResizedW == m_CurrentW && m_ResizedH == m_CurrentH))
		return true;

	m_CurrentW = m_ResizedW;
	m_CurrentH = m_ResizedH;

	UpdateRenderer(m_CurrentW, m_CurrentH);

	return true;
}

void CVideoMode::Rescale(float scale)
{
	ENSURE(m_IsInitialised);
	m_Scale = scale;
	UpdateRenderer(m_CurrentW, m_CurrentH);
}

float CVideoMode::GetScale() const
{
	return m_Scale;
}

bool CVideoMode::SetFullscreen(bool fullscreen)
{
	// This might get called before initialisation by psDisplayError;
	// if so then silently fail
	if (!m_IsInitialised)
		return false;

	// Check whether this is actually a change
	if (fullscreen == IsInFullscreen())
		return true;

	if(!SDL_SetWindowFullscreen(m_Window , fullscreen))
	{
		LOGERROR("Failed setting fullscreen");
		return false;
	}

	// #545: we need to constrain the window in fullscreen mode to avoid mouse
	// "falling out" of the window in case of multiple displays.
	if (fullscreen ? m_ConfigMouseGrabInFullscreen : m_ConfigMouseGrabInWindowMode)
		SDL_SetWindowMouseGrab(m_Window, true);
	else
		SDL_SetWindowMouseGrab(m_Window, false);

	return true;
}

bool CVideoMode::ToggleFullscreen()
{
	return SetFullscreen(!IsInFullscreen());
}

bool CVideoMode::IsInFullscreen() const
{
	return SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN;
}

void CVideoMode::UpdateRenderer(int w, int h)
{
	if (w < 2) w = 2; // avoid GL errors caused by invalid sizes
	if (h < 2) h = 2;

	m_CurrentW = w;
	m_CurrentH = h;

	SViewPort vp = { 0, 0, w, h };

	if (g_VideoMode.m_BackendDevice)
	{
		// TODO: implement freeing window size dependent resources before
		// waiting to reduce a memory spike.

		// We need to wait until we can destroy the swapchain because it
		// might be in use.
		g_VideoMode.m_BackendDevice->WaitUntilIdle();

		g_VideoMode.RecreateSwapChain();
	}

	if (CRenderer::IsInitialised())
		g_Renderer.Resize(w, h);

	if (g_GUI)
		g_GUI->UpdateResolution();

	if (g_Console)
		g_Console->UpdateScreenSize(w, h);

	if (g_Game)
		g_Game->GetView()->SetViewport(vp);
}

int CVideoMode::GetBestBPP()
{
	if (m_ConfigBPP)
		return m_ConfigBPP;
	if (m_PreferredBPP)
		return m_PreferredBPP;
	return 32;
}

int CVideoMode::GetWindowWidth() const
{
	ENSURE(m_IsInitialised);
	return m_CurrentW;
}

int CVideoMode::GetWindowHeight() const
{
	ENSURE(m_IsInitialised);
	return m_CurrentH;
}

int CVideoMode::GetBPP() const
{
	ENSURE(m_IsInitialised);
	return m_CurrentBPP;
}

bool CVideoMode::IsVSyncEnabled() const
{
	ENSURE(m_IsInitialised);
	return m_ConfigVSync;
}

int CVideoMode::GetDesktopXRes() const
{
	ENSURE(m_IsInitialised);
	return m_PreferredW;
}

int CVideoMode::GetDesktopYRes() const
{
	ENSURE(m_IsInitialised);
	return m_PreferredH;
}

int CVideoMode::GetDesktopBPP() const
{
	ENSURE(m_IsInitialised);
	return m_PreferredBPP;
}

int CVideoMode::GetDesktopFreq() const
{
	ENSURE(m_IsInitialised);
	return m_PreferredFreq;
}

SDL_Window* CVideoMode::GetWindow()
{
	ENSURE(m_IsInitialised);
	return m_Window;
}

bool CVideoMode::IsInitialized() const
{
	return m_IsInitialised;
}

void CVideoMode::SetWindowIcon()
{
	// The window icon should be kept outside of art/textures/, or else it will be converted
	// to DDS by the archive builder and will become unusable here. Using DDS makes BGRA
	// conversion needlessly complicated.
	std::shared_ptr<u8> iconFile;
	size_t iconFileSize;
	if (g_VFS->LoadFile("art/icons/window.png", iconFile, iconFileSize) != INFO::OK)
	{
		LOGWARNING("Window icon not found.");
		return;
	}

	Tex iconTexture;
	if (iconTexture.decode(iconFile, iconFileSize) != INFO::OK)
		return;

	// Convert to required BGRA format.
	const size_t iconFlags = (iconTexture.m_Flags | TEX_BGR) & ~TEX_DXT;
	if (iconTexture.transform_to(iconFlags) != INFO::OK)
		return;

	void* bgra_img = iconTexture.get_data();
	if (!bgra_img)
		return;

	SDL_Surface *iconSurface = SDL_CreateSurfaceFrom(
		iconTexture.m_Width, iconTexture.m_Height,
		SDL_GetPixelFormatForMasks(32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000),
		bgra_img, iconTexture.m_Width * 4);
	if (!iconSurface)
		return;

	SDL_SetWindowIcon(m_Window, iconSurface);
	SDL_DestroySurface(iconSurface);
}

void CVideoMode::SetCursor(const CStrW& name)
{
	if (m_Cursor)
		m_Cursor->SetCursor(name);
}

void CVideoMode::ResetCursor()
{
	if (m_Cursor)
		m_Cursor->ResetCursor();
}
