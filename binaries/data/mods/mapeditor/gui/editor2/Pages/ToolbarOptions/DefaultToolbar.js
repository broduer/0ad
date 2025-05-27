import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { StateNames } from "gui/editor2/Constants/ToolsAtlasController.js";
export class DefaultButton
{
	constructor(toolbar, button, icon)
	{
		this.button = button;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		icon.sprite = this.IconSprite;
	}

	onToogle(isToogle)
	{
		if (isToogle)
			this.setupWindow.controllers[ControllerNames.TOOLS_ATLAS_CONTROLLER].changeState(StateNames.IDLE);
	}
}

DefaultButton.prototype.IconSprite = "stretched:editor/toolbar/default.png";
DefaultButton.prototype.Tooltip = translate("Default");
DefaultButton.DEFAULT = true;
