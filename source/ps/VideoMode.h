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

#ifndef INCLUDED_VIDEOMODE
#define INCLUDED_VIDEOMODE

#include "ps/CStrForward.h"
#include "ps/Input.h"
#include "renderer/backend/Backend.h"

#include <memory>

class CStrW;
namespace Renderer::Backend { class IDevice; }
namespace Renderer::Backend { class ISwapChain; }

typedef struct SDL_Window SDL_Window;

class CVideoMode
{
public:
	CVideoMode();
	~CVideoMode();

	/**
	 * Initialise the video mode, for use in an SDL-using application.
	 */
	bool InitSDL();

	/**
	 * Initialise parts of the video mode, for use in Atlas (which uses
	 * wxWidgets instead of SDL for GL).
	 */
	bool InitNonSDL();

	/**
	 * Shut down after InitSDL/InitNonSDL, so that they can be used again.
	 */
	void Shutdown();

	/**
	 * Creates a backend device. Also we use wxWidgets in Atlas so we don't need
	 * to create one for that case.
	 */
	bool CreateBackendDevice(const bool createSDLContext);

	/**
	 * Resize the SDL window and associated graphics stuff to the new size.
	 */
	bool OnceAFrameWork();

	/**
	 * Set scale and tell dependent compoenent to recompute sizes.
	 */
	void Rescale(float scale);

	/**
	 * Switch to fullscreen or windowed mode.
	 */
	bool SetFullscreen(bool fullscreen);

	/**
	* Returns true if window runs in fullscreen mode.
	*/
	bool IsInFullscreen() const;

	/**
	 * Switch between fullscreen and windowed mode.
	 */
	bool ToggleFullscreen();

	/**
	 * Update the graphics code to start drawing to the new size.
	 * This should be called after the GL context has been resized.
	 * This can also be used when the GL context is managed externally, not via SDL.
	 */
	void UpdateRenderer(int windowWidth, int windowHeight);

	int GetWindowWidth() const;
	int GetWindowHeight() const;
	/**
	 * Returns the maximum allowable mouse X coordinate within the window bounds.
	 * When mouse grab is active, this is constrained to m_CurrentW - 1 to match
	 * the active mouse grab rectangle.
	 */
	int GetMaxMouseX() const;
	/**
	 * Returns the maximum allowable mouse Y coordinate within the window bounds.
	 * When mouse grab is active, this is constrained to m_CurrentH - 1 to match
	 * the active mouse grab rectangle.
	 */
	int GetMaxMouseY() const;
	int GetBPP() const;

	bool IsVSyncEnabled() const;
	bool IsInitialized() const;

	int GetDesktopXRes() const;
	int GetDesktopYRes() const;
	int GetDesktopBPP() const;
	int GetDesktopFreq() const;

	float GetScale() const;

	SDL_Window* GetWindow();

	void SetWindowIcon();

	void SetCursor(const CStrW& name);
	void ResetCursor();

	/**
	 * Sets SDL mouse grab for m_Window and updates the mouse grab rectangle accordingly.
	 */
	void SetWindowGrab(bool grab);
	/**
	 * Updates the SDL mouse rectangle for m_Window.
	 *
	 * Both width and height are set to 1 pixel less than m_CurrentW and m_CurrentH.
	 * On Windows, placing the cursor directly at m_CurrentW or m_CurrentH triggers
	 * resize area, and is detected as mouse leaving the window, even when mouse
	 * grab is enabled. See: <https://github.com/libsdl-org/SDL/issues/16484>, <https://gitea.wildfiregames.com/0ad/0ad/issues/9324#issuecomment-148735>
	 */
	void UpdateGrabRect();

	Renderer::Backend::IDevice* GetBackendDevice() { return m_BackendDevice.get(); }

	/**
	 * @return A swapchain (may be invalid) if available. It's not allowed to
	 * call the function during a frame rendering when an old swapchain is
	 * already in use.
	 */
	Renderer::Backend::ISwapChain* GetOrCreateSwapChain();

private:
	void ReadConfig();
	int GetBestBPP();
	bool SetVideoMode(int w, int h, int bpp, bool fullscreen);

	bool TryCreateBackendDevice(SDL_Window* window);
	void DowngradeBackendSettingAfterCreationFailure();

	/**
	 * Immediately recreates a swapchain. It's a caller's responsibility to
	 * wait till the swapchain isn't in use anymore.
	 */
	void RecreateSwapChain();

	/**
	 * Remember whether Init has been called. (This isn't used for anything
	 * important, just for verifying that the callers call our methods in
	 * the right order.)
	 */
	bool m_IsInitialised = false;

	SDL_Window* m_Window = nullptr;
public:
	Input::Manager m_InputManager;

private:
	// Initial desktop settings.
	// Frequency is in Hz, and BPP means bits per pixels (not bytes per pixels).
	int m_PreferredW = 0;
	int m_PreferredH = 0;
	int m_PreferredBPP = 0;
	int m_PreferredFreq = 0;

	float m_Scale = 1.0f;

	// Config file settings (0 if unspecified)
	int m_ConfigW = 0;
	int m_ConfigH = 0;
	int m_ConfigBPP = 0;
	int m_ConfigDisplay = 0;
	bool m_ConfigEnableHiDPI = false;
	bool m_ConfigVSync = false;

	// (m_ConfigFullscreen defaults to false, so users don't get stuck if
	// e.g. half the filesystem is missing and the config files aren't loaded).
	bool m_ConfigFullscreen = false;

	// If we're fullscreen, size/position of window when we were last windowed (or the default window
	// size/position if we started fullscreen), to support switching back to the old window size/position
	int m_WindowedW;
	int m_WindowedH;
	int m_WindowedX;
	int m_WindowedY;

	// The last mode selected
	int m_CurrentW;
	int m_CurrentH;
	int m_CurrentBPP;

	class CCursor;
	std::unique_ptr<CCursor> m_Cursor;

	Renderer::Backend::Backend m_Backend = Renderer::Backend::Backend::GL;
	std::unique_ptr<Renderer::Backend::IDevice> m_BackendDevice;
	// SwapChain for the corresponding device.
	std::unique_ptr<Renderer::Backend::ISwapChain> m_SwapChain;

	// To avoid redundant and/or recursive resizing, we save the new size after VIDEORESIZE messages and
	// only update the video mode once per frame.
	// These values are the values of the latest resize message.
	int m_ResizedW{0};
	int m_ResizedH{0};

	struct InputHandler
	{
		CVideoMode& videoMode;
		Input::Reaction operator()(const SDL_Event& ev);
	};
	Input::Handler<InputHandler> m_InputHandler{m_InputManager, Input::Slot::WINDOW, {*this}};
};

extern CVideoMode g_VideoMode;

#endif // INCLUDED_VIDEOMODE
