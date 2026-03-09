/**
 * Generic superclass for tutorial panels that manages:
 * 	- a text object showing a step's main content
 * 	- a hint object giving hints or showing warnings
 * 	- a button to continue the tutorial or quit when it's over
 */
class TutorialPanel
{
	panel;
	text;
	hint;
	button;

	closePage;

	constructor(name, closePage)
	{
		this.panel = Engine.GetGUIObjectByName(name);
		this.text = Engine.GetGUIObjectByName(name + "Text");
		this.hint = Engine.GetGUIObjectByName(name + "Hint");
		this.button = Engine.GetGUIObjectByName(name + "Button");

		this.button.caption = this.ButtonCaptions.Continue;
		this.button.onPress = () =>
		{
			Engine.PostNetworkCommand({ "type": "dialog-answer", "tutorial": "continue" });
		};

		this.closePage = closePage;
	}

	setVisible(visible)
	{
		this.panel.hidden = !visible;
	}

	displayWarning(warning)
	{
		this.hint.caption = coloredText(warning, this.WarningColor);
	}

	displayStep(step)
	{
		this.text.caption = step.text;

		if (step.showContinueButton)
		{
			this.button.hidden = false;
			if (step.isLast)
			{
				this.hint.caption = this.HintCaptions.Quit;
				this.button.caption = this.ButtonCaptions.Quit;
				this.button.onPress = this.closePage;
			}
			else
				this.hint.caption = this.HintCaptions.Continue;
		}
		else
		{
			this.hint.caption = this.HintCaptions.Instruction;
			this.button.hidden = true;
		}
	}
}

TutorialPanel.prototype.WarningColor = "orange";

TutorialPanel.prototype.ButtonCaptions = {
	"Quit": translateWithContext("button caption", "Quit"),
	"Continue": translateWithContext("button caption", "Continue")
};

TutorialPanel.prototype.HintCaptions = {
	"Continue": translate("Click to continue."),
	"Instruction": translate("Follow the instructions."),
	"Quit": translate("Click to quit this tutorial.")
};
