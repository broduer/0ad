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

	constructor(name, continueCallback)
	{
		this.panel = Engine.GetGUIObjectByName(name);
		this.text = Engine.GetGUIObjectByName(name + "Text");
		this.hint = Engine.GetGUIObjectByName(name + "Hint");
		this.button = Engine.GetGUIObjectByName(name + "Button");

		this.button.caption = this.ButtonCaptions.Continue;
		this.button.onPress = continueCallback;
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
		this.button.hidden = !step.showContinueButton;

		if (step.isLast)
		{
			this.hint.caption = this.HintCaptions.Quit;
			this.button.caption = this.ButtonCaptions.Quit;
			return;
		}

		this.hint.caption = step.isDone ?
			this.HintCaptions.Done :
			step.showContinueButton ?
				this.HintCaptions.Continue :
				this.HintCaptions.Instruction;
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
	"Done": translate("You have already done this."),
	"Quit": translate("Click to quit this tutorial.")
};
