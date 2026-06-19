/**
 * The purpose of this class is to exclusively manage the diplomacy colors button within the minimap.
 */
class MiniMapDiplomacyColorsButton
{
	constructor(diplomacyColors)
	{
		this.buttons = {
			"square": Engine.GetGUIObjectByName("diplomacyColorsButtonSquare"),
			"round": Engine.GetGUIObjectByName("diplomacyColorsButtonRound")
		};

		for (const shape in this.buttons)
			this.buttons[shape].onPress = diplomacyColors.toggle.bind(diplomacyColors);

		Engine.SetGlobalHotkey(this.Hotkey, "Press", diplomacyColors.toggle.bind(diplomacyColors));

		this.onDiplomacyColorsChange(diplomacyColors.isEnabled());
		diplomacyColors.registerDiplomacyColorsChangeHandler(this.onDiplomacyColorsChange.bind(this));
		registerHotkeyChangeHandler(this.onHotkeyChange.bind(this));
	}

	onHotkeyChange()
	{
		const tooltip = colorizeHotkey("%(hotkey)s" + " ", this.Hotkey) + translate(this.Tooltip);
		this.buttons.square.tooltip = tooltip;
		this.buttons.round.tooltip = tooltip;
	}

	onDiplomacyColorsChange(enabled)
	{
		const sprites = this.Sprites;
		for (const shape in this.buttons)
		{
			this.buttons[shape].sprite = enabled ? sprites[shape].on : sprites[shape].off;
			this.buttons[shape].sprite_over = enabled ? sprites[shape].on_over : sprites[shape].off_over;
		}
	}
}

MiniMapDiplomacyColorsButton.prototype.Hotkey = "session.diplomacycolors";

MiniMapDiplomacyColorsButton.prototype.Tooltip = markForTranslation("Toggle Diplomacy Colors");

MiniMapDiplomacyColorsButton.prototype.Sprites = {
	"square": {
		"on": "stretched:session/minimap/square/diplomacy-on.png",
		"off": "stretched:session/minimap/square/diplomacy-off.png",
		"on_over": "stretched:session/minimap/square/diplomacy-on_highlight.png",
		"off_over": "stretched:session/minimap/square/diplomacy-off_highlight.png"
	},
	"round": {
		"on": "stretched:session/minimap/round/diplomacy-on.png",
		"off": "stretched:session/minimap/round/diplomacy-off.png",
		"on_over": "stretched:session/minimap/round/diplomacy-on_highlight.png",
		"off_over": "stretched:session/minimap/round/diplomacy-off_highlight.png"
	}
};
