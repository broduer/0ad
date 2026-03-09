/**
 * This class manages a tutorial panel meant to display basic instructions of simple tasks for the player to fulfill,
 * consisting of just a text, like "Order one of your units to build a house."
 */
class InstructionPanel extends TutorialPanel
{
	instructions = [];

	constructor(closePage)
	{
		super("instructionPanel", closePage);
	}

	displayStep(step)
	{
		super.displayStep(step);

		const panelHeight = Math.min(this.MaxPanelHeight, this.text.size.top + this.text.getTextSize().height - this.text.size.bottom);
		this.panel.size.bottom = this.panel.getComputedSize().top + panelHeight;
	}
}

InstructionPanel.prototype.MaxPanelHeight = 185;
