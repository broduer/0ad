// Needs to be kept in sync with the one in maps/scripts/Tutorial.js
const TUTORIAL_STEP_TYPE = deepfreeze({
	"INSTRUCTION": 1,
	"INFO": 2,
	"GUI_EXPLANATION": 3
});

/**
 * This class manages the tutorial UI, which consists of various panels, which correspond to the different step types.
 * It processes all tutorial notifications received from the simulation, translates them and passes them along to
 * according panel.
 */
class TutorialManager
{
	parentObj = Engine.GetGUIObjectByName("tutorialPanels");

	panels = new Map([
		[TUTORIAL_STEP_TYPE.INSTRUCTION, new InstructionPanel(this.continue.bind(this))],
		[TUTORIAL_STEP_TYPE.INFO, new InfoPanel(this.continue.bind(this))],
		[TUTORIAL_STEP_TYPE.GUI_EXPLANATION, new GuiExplanationPanel(this.continue.bind(this))]
	]);

	displayedSteps = []; // All steps that have already been displayed, in the form of [stepType, panelData]
	pendingSteps = []; // All received steps that have yet to be displayed, in the form of [stepType, panelData]
	activePanel;
	currentWarning = "";
	isFinished = false;
	closeSession;

	constructor(closeSession, hotloadData)
	{
		this.closeSession = closeSession;

		this.parentObj.hidden = true;

		if (hotloadData)
		{
			for (const [stepType, panelData] of hotloadData.displayedSteps)
				this.displayStep(stepType, panelData);

			this.pendingSteps = hotloadData.pendingSteps;
			if (hotloadData.warning)
				this.displayWarning(hotloadData.warning);
		}
	}

	toggleVisibility()
	{
		this.parentObj.hidden = !this.displayedSteps.length || !this.parentObj.hidden;
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
		this.currentWarning = warning;
		this.activePanel.displayWarning(translate(warning));
	}

	displayNextPendingStep()
	{
		this.displayStep(...this.pendingSteps.shift());
	}

	displayStep(stepType, panelData)
	{
		if (this.isFinished)
			return;

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

		// Explicitly don't hide the previously active panel if the new step is a GUI explanation. It's displayed
		// "on top" of everything else.
		if (stepType == TUTORIAL_STEP_TYPE.GUI_EXPLANATION)
		{
			this.panels.get(TUTORIAL_STEP_TYPE.GUI_EXPLANATION).setVisible(true);
			this.activePanel.setEnabled(false);
		}
		else
			this.panels.forEach((panel, type) => panel.setVisible(type == stepType));

		this.activePanel = this.panels.get(stepType);
		this.activePanel.setEnabled(true);
		this.activePanel.displayStep(panelData);

		this.displayedSteps.push([stepType, panelData]);
		this.currentWarning = "";
	}

	continue()
	{
		if (this.isFinished)
			this.closeSession(true);
		else if (this.pendingSteps.length)
			this.displayNextPendingStep();
		else
			Engine.PostNetworkCommand({ "type": "dialog-answer", "tutorial": "continue" });
	}

	getHotloadData()
	{
		return {
			"displayedSteps": this.displayedSteps,
			"pendingSteps": this.pendingSteps,
			"warning": this.currentWarning
		};
	}
}
