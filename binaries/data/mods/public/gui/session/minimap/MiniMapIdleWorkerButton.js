/**
 * If the button that this class manages is pressed, an idle unit having one of the given classes is selected.
 */
class MiniMapIdleWorkerButton
{
	constructor(playerViewControl, idleClasses)
	{
		this.buttons = {
			"square": Engine.GetGUIObjectByName("idleWorkerButtonSquare"),
			"round": Engine.GetGUIObjectByName("idleWorkerButtonRound")
		};
		this.labels = {
			"square": Engine.GetGUIObjectByName("totalNumberIdleWorkersSquare"),
			"round": Engine.GetGUIObjectByName("totalNumberIdleWorkersRound")
		};
		this.idleClasses = idleClasses;

		for (const shape in this.buttons)
			this.buttons[shape].onMouseLeftPress = this.onKeyDown.bind(this);

		Engine.SetGlobalHotkey(this.Hotkey, "KeyDown", this.onKeyDown.bind(this));
		registerHotkeyChangeHandler(this.onHotkeyChange.bind(this));
		registerSimulationUpdateHandler(this.rebuild.bind(this));
		playerViewControl.registerViewedPlayerChangeHandler(this.rebuild.bind(this));
	}

	onHotkeyChange()
	{
		const tooltip = colorizeHotkey("%(hotkey)s" + " ", this.Hotkey) + translate(this.Tooltip);
		this.buttons.square.tooltip = tooltip;
		this.buttons.round.tooltip = tooltip;
	}

	rebuild()
	{
		const totalNumberIdleWorkers = Engine.GuiInterfaceCall("FindIdleUnits", {
			"viewedPlayer": g_ViewedPlayer,
			"idleClasses": this.idleClasses,
			"excludeUnits": []
		}).length;
		const enabled = totalNumberIdleWorkers > 0;
		const caption = totalNumberIdleWorkers ? setStringTags(totalNumberIdleWorkers, this.DefaultTotalNumberIdleWorkersTags) : "";
		for (const shape in this.buttons)
		{
			this.buttons[shape].enabled = enabled;
			this.labels[shape].caption = caption;
		}
	}

	onKeyDown()
	{
		if (this.buttons.square.enabled)
			findIdleUnit(this.idleClasses);
	}
}

MiniMapIdleWorkerButton.prototype.Hotkey = "selection.idleworker";

MiniMapIdleWorkerButton.prototype.Tooltip = markForTranslation("Find idle worker\nNumber of idle workers.");

MiniMapIdleWorkerButton.prototype.DefaultTotalNumberIdleWorkersTags = {
	"font": "sans-bold-stroke-14",
	"color": "white"
};
