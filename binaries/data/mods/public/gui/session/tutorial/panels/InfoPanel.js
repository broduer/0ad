/**
 * This class manages a tutorial panel meant to explain game concepts or mechanics to the player, like selecting units
 * or bartering, for example.
 * These explanations consist of a title and paragraphs of texts. New steps to display don't have to replace the
 * preceding ones; with the 'appendable' flag they can add a paragraph to the bottom of an existing explanation instead
 * making a brand new one.
 */
class InfoPanel extends TutorialPanel
{
	title = Engine.GetGUIObjectByName("infoPanelTitle");

	paragraphs = [];
	appendable = true;

	constructor(continueCallback)
	{
		super("infoPanel", continueCallback);
	}

	displayStep(step)
	{
		super.displayStep(step);

		if (step.appendToPrevious && !this.appendable)
		{
			error("InfoPanel: Can't append step to previous because previous is marked unappendable.");
			return;
		}

		if (!step.appendToPrevious)
		{
			this.paragraphs = [];
			if (step.title)
				this.title.caption = step.title;
			else
				this.title.hidden = true;
		}

		// Appendable being false means that no paragraph will be appended to this (first one) in the future.
		// In that case, we want to portray it as a single piece of info rather than a list of them.
		this.appendable = step.appendToPrevious || step.appendable;
		if (this.appendable)
		{
			this.text.text_align = "left";
			const newParagraph = sprintf(this.BulletPointFormat, { "text": step.text });
			this.text.caption = this.paragraphs.length ?
				this.paragraphs.concat(coloredText(newParagraph, this.NewParagraphColor)).join("\n") :
				newParagraph;
			this.paragraphs.push(newParagraph);
		}
		else
		{
			this.text.text_align = "center";
			this.text.caption = step.text;
		}

		this.text.size.top = this.title.hidden || !this.title.caption ? 27 : 54;
		const panelHeight = Math.min(this.MaxPanelHeight, this.text.size.top + this.text.getTextSize().height - this.text.size.bottom);
		this.panel.size.bottom = this.panel.getComputedSize().top + panelHeight;
	}
}

InfoPanel.prototype.BulletPointFormat = translateWithContext("bullet point format", " •  %(text)s");
InfoPanel.prototype.NewParagraphColor = "gold";

InfoPanel.prototype.MaxPanelHeight = 235;
