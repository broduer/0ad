/**
 * This class manages a tutorial panel meant to display basic instructions of simple tasks for the player to fulfill,
 * consisting of just a text, like "Order one of your units to build a house."
 */
class InstructionPanel
{
	panel = Engine.GetGUIObjectByName("instructionPanel");
	text = Engine.GetGUIObjectByName("instructionPanelText");
	hint = Engine.GetGUIObjectByName("instructionPanelHint");
	continueButton = Engine.GetGUIObjectByName("instructionPanelContinueButton");
	instructions = [];
	closePage;

	constructor(closePage)
	{
		this.closePage = closePage;
		this.continueButton.onPress = () =>
		{
			Engine.PostNetworkCommand({ "type": "dialog-answer", "tutorial": "continue" });
		};
	}

	setVisible(visible)
	{
		this.panel.hidden = !visible;
	}

	displayWarning(warning)
	{
		this.hint.caption = setStringTags(warning, this.WarningTags);
	}

	displayStep(panelData)
	{
		this.text.caption = panelData.text;
		const panelHeight = Math.min(this.MaxPanelHeight, this.text.size.top + this.text.getTextSize().height - this.text.size.bottom);
		this.panel.size.bottom = this.panel.getComputedSize().top + panelHeight;

		if (panelData.showContinueButton)
		{
			this.continueButton.hidden = false;
			if (panelData.isLast)
			{
				this.hint.caption = translate("Click to quit this tutorial.");
				this.continueButton.caption = translate("Quit");
				this.continueButton.onPress = this.closePage;
			}
			else
				this.hint.caption = this.HintCaptions.Continue;
		}
		else
		{
			this.hint.caption = this.HintCaptions.Instruction;
			this.continueButton.hidden = true;
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

InstructionPanel.prototype.HintCaptions = {
	"Continue": translate("Click when continue."),
	"Instruction": translate("Follow the instructions."),
	"Quit": translate("Click to quit this tutorial.")
};

InstructionPanel.prototype.MaxPanelHeight = 185;
