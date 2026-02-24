class Tutorial
{
	panel = Engine.GetGUIObjectByName("tutorialPanel");
	text = Engine.GetGUIObjectByName("tutorialText");
	warning = Engine.GetGUIObjectByName("tutorialWarning");
	readyButton = Engine.GetGUIObjectByName("tutorialReady");
	instructions = [];
	closePageCallback;

	constructor(closePageCallback)
	{
		this.closePageCallback = closePageCallback;
	}

	toggle()
	{
		this.panel.hidden = !this.panel.hidden || !this.text.caption;
	}

	handleNotification(notification)
	{
		if (notification.goal)
			this.displayGoal(notification.goal);
		if (notification.warning)
			this.displayWarning(notification.warning);
	}

	displayWarning(warning)
	{
		this.warning.caption = coloredText(translate(warning), "orange");
	}

	displayGoal(goal)
	{
		this.panel.hidden = false;

		const notificationText =
			goal.instructions.reduce((instructions, item) =>
				instructions + (typeof item === "string" ? translate(item) : colorizeHotkey(translate(item.text), item.hotkey)),
			"");

		this.text.caption = this.instructions.concat(setStringTags(notificationText, this.NewInstructionTags)).join("\n");
		this.instructions.push(notificationText);

		if (goal.readyButton)
		{
			this.readyButton.hidden = false;
			if (goal.leave)
			{
				this.warning.caption = translate("Click to quit this tutorial.");
				this.readyButton.caption = translate("Quit");
				this.readyButton.onPress = () => { this.closePageCallback({ [Engine.openRequest]: endGame(true) }); };
			}
			else
				this.warning.caption = translate("Click when ready.");
		}
		else
		{
			this.warning.caption = translate("Follow the instructions.");
			this.readyButton.hidden = true;
		}
	}
}

/**
 * GUI tags applied to the most recent instruction.
 */
Tutorial.prototype.NewInstructionTags = { "color": "255 226 149" };

