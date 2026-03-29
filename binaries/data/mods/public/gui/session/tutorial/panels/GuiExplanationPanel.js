/**
 * This class manages a tutorial panel meant to explain GUI elements (what they show or do) to the player, like
 * "The resource counters at the top show how many resources you have ...".
 * The explanations consist of a title and text. It also highlights the target GUI objects by fading out everything else
 * with black and uses an arrow to point to them directly.
 * If the target object is hidden the panel is still shown (pointing to nothing), but a warning displayed.
 */
class GuiExplanationPanel extends TutorialPanel
{
	panel = Engine.GetGUIObjectByName("guiExplanationPanel");
	backgroundFade = Engine.GetGUIObjectByName("guiExplanationPanelFade");
	arrow = Engine.GetGUIObjectByName("guiExplanationPanelArrow");

	currentStep;
	targetObject;
	wasTargetHidden = false;

	constructor(continueCallback)
	{
		super("guiExplanationPanel", () =>
		{
			this.resetTargetObjectZ();
			this.targetObject = null;
			continueCallback();
		});

		// Place the panel in front of all other session objects.
		this.panel.z = this.OverlayZValue + 10;
		// Place the black overlay behind the panel.
		this.backgroundFade.z = this.OverlayZValue - 10;

		// We need to set the arrow's Z value explicitly since it has 'absolute' set to true, which makes it not inherit
		// the parent's value by default.
		this.arrow.z = this.OverlayZValue;

		// Initialize the size to a default, with all relative values set to 0.
		// This is necessary for the custom sizing later.
		this.panel.size = { "right": this.PanelWidth, "bottom": this.MaxPanelHeight };
		this.arrow.size = { "right": this.ArrowScale * 2, "bottom": this.ArrowScale };

		this.panel.onWindowResized = this.updatePanelSize.bind(this);
		this.panel.onTick = this.updateContent.bind(this);
	}

	setVisible(visible)
	{
		super.setVisible(visible);
		if (!visible)
			this.resetTargetObjectZ();
	}

	resetTargetObjectZ()
	{
		if (this.targetObject)
			this.targetObject.z = this.targetOriginalZ;
	}

	displayStep(step)
	{
		super.displayStep(step);

		this.resetTargetObjectZ();

		if (!["left", "top", "right", "bottom"].includes(step.side))
			throw new Error("GuiExplanationPanel: Invalid or no side specified:" + step.side + ". It must be 'left', 'top', 'right', or 'bottom'.");

		this.targetObject = Engine.TryGetGUIObjectByName(step.targetObject);
		if (!this.targetObject)
			throw new Error("GuiExplanationPanel: Non-existing target GUI object specified: '" + step.targetObject + "'.");

		this.currentStep = step;

		// This results in the target's grandchildren taking higher Z values than this panel (since they get their parent's value + 10).
		// But that's ok, since they should never overlap anyway.
		this.targetOriginalZ = this.targetObject.z;
		this.targetObject.z = this.OverlayZValue;

		this.arrow.sprite = this.ArrowSprites[step.side];

		this.wasTargetHidden = undefined;
		this.updateContent();
	}

	/**
	 * Check the visibility of the target object and update the text and background fade accordingly, if necessary.
	 */
	updateContent()
	{
		if (!this.targetObject)
			return;

		const displayTextAndTitle = (title, text) =>
		{
			this.text.caption = (title ? setStringTags(title, this.TitleTags) + setStringTags("\n\n", { "font": "sans-3" }) : "") + text;
			this.updatePanelSize();
		};

		if (this.targetObject.hidden && !this.wasTargetHidden)
		{
			this.backgroundFade.hidden = true;
			displayTextAndTitle(this.currentStep.title, this.HiddenWarning);
			this.wasTargetHidden = true;
		}
		else if (!this.targetObject.hidden && (this.wasTargetHidden == undefined || this.wasTargetHidden))
		{
			this.backgroundFade.hidden = false;
			displayTextAndTitle(this.currentStep.title, this.currentStep.text);
			this.wasTargetHidden = false;
		}
	}

	/**
	 * Place the panel next to the target object on the desired side relative to it.
	 */
	updatePanelSize()
	{
		if (!this.targetObject)
			return;

		const panelHeight = Math.min(this.MaxPanelHeight, this.text.size.top + this.text.getTextSize().height - this.text.size.bottom);
		const targetComputedSize = this.targetObject.getComputedSize();
		const targetHorizontalCenter = (targetComputedSize.left + targetComputedSize.right) / 2;
		const targetVerticalCenter = (targetComputedSize.top + targetComputedSize.bottom) / 2;

		// Don't perfectly center the panel on the target object, instead shift it to one side a bit.
		// That looks better.
		const panelPlacementRatio = 0.3;

		if (this.currentStep.side == "top" || this.currentStep.side == "bottom")
		{
			this.panel.size.left = targetHorizontalCenter - panelPlacementRatio * this.PanelWidth;
			this.panel.size.right = targetHorizontalCenter + (1 - panelPlacementRatio) * this.PanelWidth;
			this.arrow.size.left = targetHorizontalCenter - this.ArrowScale;
			this.arrow.size.right = targetHorizontalCenter + this.ArrowScale;

			if (this.currentStep.side == "top")
			{
				this.panel.size.bottom = targetComputedSize.top - this.PanelDistanceFromTarget;
				this.panel.size.top = this.panel.size.bottom - panelHeight;
				this.arrow.size.bottom = targetComputedSize.top - this.ArrowDistanceFromTarget;
				this.arrow.size.top = this.arrow.size.bottom - this.ArrowScale;
			}
			else
			{
				this.panel.size.top = targetComputedSize.bottom + this.PanelDistanceFromTarget;
				this.panel.size.bottom = this.panel.size.top + panelHeight;
				this.arrow.size.top = targetComputedSize.bottom + this.ArrowDistanceFromTarget;
				this.arrow.size.bottom = this.arrow.size.top + this.ArrowScale;
			}
		}
		else
		{
			this.panel.size.top = targetVerticalCenter - panelPlacementRatio * panelHeight;
			this.panel.size.bottom = targetVerticalCenter + (1 - panelPlacementRatio) * panelHeight;
			this.arrow.size.top = targetVerticalCenter - this.ArrowScale;
			this.arrow.size.bottom = targetVerticalCenter + this.ArrowScale;

			if (this.currentStep.side == "left")
			{
				this.panel.size.right = targetComputedSize.left - this.PanelDistanceFromTarget;
				this.panel.size.left = this.panel.size.right - this.PanelWidth;
				this.arrow.size.right = targetComputedSize.left - this.ArrowDistanceFromTarget;
				this.arrow.size.left = this.arrow.size.right - this.ArrowScale;
			}
			else
			{
				this.panel.size.left = targetComputedSize.right + this.PanelDistanceFromTarget;
				this.panel.size.right = this.panel.size.left + this.PanelWidth;
				this.arrow.size.left = targetComputedSize.right + this.ArrowDistanceFromTarget;
				this.arrow.size.right = this.arrow.size.left + this.ArrowScale;
			}
		}

		let root = this.panel;
		while (root.parent) root = root.parent;
		const screenSize = root.getComputedSize();

		// Perform some clamping to ensure the panel is always fully visible on the screen.
		if (this.panel.size.left < 0)
		{
			this.panel.size.right -= this.panel.size.left;
			this.panel.size.left = 0;
		}
		else if (this.panel.size.right > screenSize.right)
		{
			this.panel.size.left -= this.panel.size.right - screenSize.right;
			this.panel.size.right = screenSize.right;
		}
		if (this.panel.size.top < 0)
		{
			this.panel.size.bottom -= this.panel.size.top;
			this.panel.size.top = 0;
		}
		else if (this.panel.size.bottom > screenSize.bottom)
		{
			this.panel.size.top -= this.panel.size.bottom - screenSize.bottom;
			this.panel.size.bottom = screenSize.bottom;
		}
	}
}

/**
 * Warning shown whilever the target objects is hidden.
 */
GuiExplanationPanel.prototype.HiddenWarning = coloredText(
	translate("It is currently hidden. Follow the previous instructions for it to be shown again."),
	"orange"
);

/**
 * GUI tags applied to the shown title.
 */
GuiExplanationPanel.prototype.TitleTags = { "color": "gold", "font": "sans-bold-18" };

/**
 * Which sprite to use depending on the side that the description panel is placed on. The sprites only differ in rotation.
 * E.g. if the panel is placed to the left of the target object, the arrow has to point to the right.
 */
GuiExplanationPanel.prototype.ArrowSprites = {
	"left": "GoldenArrowRight",
	"top": "GoldenArrowDown",
	"right": "GoldenArrowLeft",
	"bottom": "GoldenArrowUp"
};

GuiExplanationPanel.prototype.PanelWidth = 600;
GuiExplanationPanel.prototype.MaxPanelHeight = 200;

/**
 * Margin between the target object and the panel itself. The arrow has to fit inside that gap.
 */
GuiExplanationPanel.prototype.PanelDistanceFromTarget = 40;

/**
 * Margin between the target object and the arrow.
 */
GuiExplanationPanel.prototype.ArrowDistanceFromTarget = 5;

/**
 * Greater than the z value of all other session GUI objects (except the watermark).
 */
GuiExplanationPanel.prototype.OverlayZValue = 250;

/**
 * The arrow is always twice as wide as it is long.
 * Therefore, for vertical arrows (pointing up or down) this defines the height and half the width,
 * while for horizontal arrows (pointing left or right) this defines the width and half the height.
 */
GuiExplanationPanel.prototype.ArrowScale = 32;
