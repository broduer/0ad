/**
 * This class manages a tutorial panel meant to display basic instructions of simple tasks for the player to fulfill,
 * consisting of just a text, like "Order one of your units to build a house."
 */
class InstructionPanel
{
	panel = Engine.GetGUIObjectByName("instructionPanel");
	text = Engine.GetGUIObjectByName("instructionPanelText");
	warning = Engine.GetGUIObjectByName("instructionPanelWarning");
	readyButton = Engine.GetGUIObjectByName("instructionPanelReady");
	instructions = [];
	closePage;

	constructor(closePage)
	{
		this.closePage = closePage;
		this.readyButton.onPress = () =>
		{
			Engine.PostNetworkCommand({ "type": "dialog-answer", "tutorial": "ready" });
		};
	}

	setVisible(visible)
	{
		this.panel.hidden = !visible;
	}

	displayWarning(warning)
	{
		this.warning.caption = setStringTags(warning, this.WarningTags);
	}

	displayStep(panelData)
	{
		this.text.caption = this.instructions.concat(setStringTags(panelData.text, this.NewInstructionTags)).join("\n");
		this.instructions.push(panelData.text);

		if (panelData.readyButton)
		{
			this.readyButton.hidden = false;
			if (panelData.leave)
			{
				this.warning.caption = translate("Click to quit this tutorial.");
				this.readyButton.caption = translate("Quit");
				this.readyButton.onPress = this.closePage;
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
 * Tags applied to the most recent instruction.
 */
InstructionPanel.prototype.NewInstructionTags = { "color": "255 226 149" };

/**
 * Tags applied to warning messages.
 */
InstructionPanel.prototype.WarningTags = { "color": "orange" };
