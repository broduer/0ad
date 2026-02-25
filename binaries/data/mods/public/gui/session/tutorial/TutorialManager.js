// Needs to be kept in sync with the one in maps/scripts/Tutorial.js
const TUTORIAL_STEP_TYPE = deepfreeze({
	"INSTRUCTION": 1
});

/**
 * This class manages the tutorial UI, which consists of various panels, which correspond to the different step types.
 * It processes all tutorial notifications received from the simulation, translates them and passes them along to
 * according panel.
 */
class TutorialManager
{
	parentObj = Engine.GetGUIObjectByName("tutorialPanels");

	panels = new Map();
	activePanel;
	closePageCallback;

	constructor(closePageCallback)
	{
		this.closePageCallback = closePageCallback;

		this.panels.set(TUTORIAL_STEP_TYPE.INSTRUCTION, new InstructionPanel(this.closePage.bind(this)));
		this.parentObj.hidden = true;
	}

	toggleVisibility()
	{
		this.parentObj.hidden = !this.activePanel || !this.parentObj.hidden;
	}

	handleNotification(notification)
	{
		if (notification.step)
			this.displayStep(notification.step.type, notification.step.panelData);
		if (notification.warning)
			this.displayWarning(notification.warning);
	}

	displayWarning(warning)
	{
		this.activePanel.displayWarning(translate(warning));
	}

	displayStep(stepType, panelData)
	{
		this.parentObj.hidden = false;

		translateObjectKeys(panelData, ["text", "title"]);
		if (panelData.hotkeys)
		{
			let i = 0;
			panelData.text = panelData.hotkeys.length == 1 ?
				colorizeHotkey(panelData.text, panelData.hotkeys[0]) :
				sprintf(panelData.text, panelData.hotkeys.reduce((obj, hotkey) =>
				{
					obj["hotkey" + ++i] = colorizeHotkey("%(hotkey)s", hotkey);
					return obj;
				}, {}));
		}

		if (!this.panels.has(stepType))
			throw new Error("Failed to display tutorial step: Unkown step type: " + stepType);

		this.panels.forEach((panel, type) => panel.setVisible(type == stepType));
		this.activePanel = this.panels.get(stepType);
		this.activePanel.displayStep(panelData);
	}

	closePage()
	{
		this.closePageCallback({ [Engine.openRequest]: endGame(true) });
	}
}
