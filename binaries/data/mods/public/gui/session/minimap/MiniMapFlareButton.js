/**
 * If the button that this class manages is pressed, the input state is switched to 'flare', letting the player send a flare by left-clicking on the map.
 */
class MiniMapFlareButton
{
	constructor(playerViewControl)
	{
		this.buttons = {
			"square": Engine.GetGUIObjectByName("flareButtonSquare"),
			"round": Engine.GetGUIObjectByName("flareButtonRound")
		};
		for (const shape in this.buttons)
			this.buttons[shape].enabled = !g_IsReplay;
		if (g_IsReplay)
			return;
		for (const shape in this.buttons)
			this.buttons[shape].onPress = this.onPress.bind(this);
		Engine.SetGlobalHotkey("session.flareactivate", "Press", this.onPress.bind(this));
		registerHotkeyChangeHandler(this.onHotkeyChange.bind(this));
		playerViewControl.registerViewedPlayerChangeHandler(this.rebuild.bind(this));
	}

	updateSprites(isObserver)
	{
		const sprites = isObserver ? this.ObserverSprites : this.PlayerSprites;
		for (const key in this.buttons)
		{
			this.buttons[key].sprite = sprites[key].sprite;
			this.buttons[key].sprite_over = sprites[key].sprite_over;
		}
	}

	rebuild()
	{
		this.updateTooltip();
	}

	onHotkeyChange()
	{
		this.colorizedHotkey = colorizeHotkey("%(hotkey)s" + " ", "session.flare");
		this.updateTooltip();
	}

	updateTooltip()
	{
		const tooltip = this.colorizedHotkey + (g_IsObserver ? this.ObserverTooltip : this.PlayerTooltip);
		this.buttons.square.tooltip = tooltip;
		this.buttons.round.tooltip = tooltip;
	}

	onPress()
	{
		if (inputState == INPUT_NORMAL)
			inputState = INPUT_FLARE;
	}
}

MiniMapFlareButton.prototype.PlayerTooltip = markForTranslation("Send a flare to your allies.");
MiniMapFlareButton.prototype.ObserverTooltip = markForTranslation("Send a flare to other observers.");

MiniMapFlareButton.prototype.PlayerSprites = {
	"square": {
		"sprite": "stretched:session/minimap/square/player-flare.png",
		"sprite_over": "stretched:session/minimap/square/player-flare_highlight.png"
	},
	"round": {
		"sprite": "stretched:session/minimap/round/player-flare.png",
		"sprite_over": "stretched:session/minimap/round/player-flare_highlight.png"
	}
};

MiniMapFlareButton.prototype.ObserverSprites = {
	"square": {
		"sprite": "stretched:session/minimap/square/observer-flare.png",
		"sprite_over": "stretched:session/minimap/square/observer-flare_highlight.png"
	},
	"round": {
		"sprite": "stretched:session/minimap/round/observer-flare.png",
		"sprite_over": "stretched:session/minimap/round/observer-flare_highlight.png"
	}
};
