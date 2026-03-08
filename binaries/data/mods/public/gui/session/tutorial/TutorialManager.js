// Needs to be kept in sync with the one in maps/scripts/Tutorial.js
const TUTORIAL_STEP_TYPE = deepfreeze({
	"INSTRUCTION": 1,
	"INFO": 2
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
	pendingSteps = [];
	activePanel;
	isFinished = false;
	closePageCallback;

	constructor(closePageCallback)
	{
		this.closePageCallback = closePageCallback;

		this.panels.set(TUTORIAL_STEP_TYPE.INSTRUCTION, new InstructionPanel(this.continue.bind(this)));
		this.panels.set(TUTORIAL_STEP_TYPE.INFO, new InfoPanel(this.continue.bind(this)));
		this.parentObj.hidden = true;
	}

	toggleVisibility()
	{
		this.parentObj.hidden = !this.activePanel || !this.parentObj.hidden;
	}

	handleNotification(notification)
	{
		if (notification.step)
		{
			// The step might be defined in a way that requires us to split it up into several
			// before displaying them.

			while (this.pendingSteps.length)
				this.displayNextPendingStep();

			// To minimise duplication in the trigger script, consecutive steps that only
			// differ in the 'text' property can be combined into a single one with an array
			// 'texts'. Here we need revert this again by splitting it back into different
			// steps before passing them to the panels.
			if (Array.isArray(notification.step.panelData.texts))
			{
				const { texts, ...sharedData } = notification.step.panelData;
				this.pendingSteps = texts.map((text, i) => ([
					notification.step.type, {
						...sharedData, text,
						// Note: These could be ignored by some panels.
						"appendable": sharedData.appendable || i === 0,
						"appendToPrevious": sharedData.appendToPrevious || i > 0
					}
				]));
				this.displayNextPendingStep();
			}
			else
				this.displayStep(notification.step.type, notification.step.panelData);
		}

		if (notification.warning)
			this.displayWarning(notification.warning);
	}

	displayWarning(warning)
	{
		this.activePanel.displayWarning(translate(warning));
	}

	displayNextPendingStep()
	{
		this.displayStep(...this.pendingSteps.shift());
	}

	displayStep(stepType, panelData)
	{
		this.parentObj.hidden = false;
		this.isFinished = panelData.isLast;

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

	continue()
	{
		if (this.isFinished)
			this.closePageCallback({ [Engine.openRequest]: endGame(true) });
		else if (this.pendingSteps.length)
			this.displayNextPendingStep();
		else
			Engine.PostNetworkCommand({ "type": "dialog-answer", "tutorial": "continue" });
	}
}
