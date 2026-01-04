/**
 * This class manages the cinematic overlay, which is responsible for showing black bars at the top and bottom while
 * cinema paths are playing. Cinema paths are predefined camera animations, which block player input of any kind while
 * playing; cutscenes, essentially. Whether one is playing is communicated through the simulation state.
 */
class CinemaOverlay
{
	constructor()
	{
		this.overlay = Engine.GetGUIObjectByName("cinemaOverlay");
		this.barTop = Engine.GetGUIObjectByName("cinemaOverlayBarTop");
		this.barBottom = Engine.GetGUIObjectByName("cinemaOverlayBarBottom");

		// Objects to hide while showing the overlay.
		this.primarySessionOverlays = Engine.GetGUIObjectByName("primaryOverlays");
		this.bandbox = Engine.GetGUIObjectByName("bandbox");
		this.hotkeys = Engine.GetGUIObjectByName("hotkeys");

		this.overlay.onSimulationUpdate = this.onSimulationUpdate.bind(this);
		this.overlay.onWindowResized = () =>
		{
			this.recalculateBarSizes();
		};
		this.overlay.hidden = true;
		this.isCutsceneModeEnabled = Engine.Renderer_GetCutsceneModeEnabled();

		this.recalculateBarSizes();
	}

	/**
	 * Enable or disable cutscene mode and remember it in order to save unnecessary calls to the engine.
	 * This, however, assumes that the mode isn't modified anywhere else.
	 */
	setCutsceneModeEnabled(enabled)
	{
		if (this.isCutsceneModeEnabled == enabled)
			return;

		Engine.Renderer_SetCutsceneModeEnabled(!!enabled);
		this.isCutsceneModeEnabled = enabled;
	}

	isInCutsceneMode()
	{
		return this.isCutsceneModeEnabled;
	}

	onSimulationUpdate()
	{
		const cinemaPathPlaying = GetSimState().cinemaPathPlaying;
		if (this.overlay.hidden && cinemaPathPlaying)
			this.show();
		else if (!this.overlay.hidden && !cinemaPathPlaying)
			this.hide();
	}

	show()
	{
		if (!this.overlay.hidden)
			return;

		this.primarySessionOverlays.hidden = true;
		this.bandbox.hidden = true;
		this.hotkeys.hidden = true;
		this.overlay.hidden = false;

		this.setCutsceneModeEnabled(true);
	}

	hide()
	{
		if (this.overlay.hidden)
			return;

		this.primarySessionOverlays.hidden = !g_ShowGUI;
		this.hotkeys.hidden = false;
		this.overlay.hidden = true;

		this.setCutsceneModeEnabled(false);
	}

	recalculateBarSizes()
	{
		const minHeight = 115;
		const width = this.overlay.getComputedSize().right;
		const height = this.overlay.getComputedSize().bottom;
		// The aspect ratio of 2.39:1 is typical in films and has a cinematic feel to it.
		const barHeight = Math.max(minHeight, (height - width / 2.39) / 2);
		this.barTop.size.bottom = barHeight;
		this.barBottom.size.top = -barHeight;
	}
}
