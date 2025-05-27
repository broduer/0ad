import { ControllerNames } from "gui/editor2/Constants/Controllers.js";

export class PaintTerrainButton
{
	constructor(toolbar, button, icon, index)
	{
		this.button = button;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		icon.sprite = this.IconSprite;

		this.index = index;
		this.toolName = "PaintTerrain"; // TODO: Replace hardcode toolName
		this.isToogle = false;

		this.setupWindow.registerLoadHandler(()=>
		{
			this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER].watch(()=>
			{
				if (this.isToogle && this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER].tool !== this.toolName)
					toolbar.updateToogleWithoutEvent(this.index, this.isToogle = false);
				else if (!this.isToogle && this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER].tool === this.toolName)
					toolbar.updateToogleWithoutEvent(this.index, this.isToogle = true);
			}, ["tool"]);
		});
	}

	onToogle(isToogle)
	{
		this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER].tool = isToogle ? this.toolName : "";
		this.isToogle = isToogle;
	}
}

PaintTerrainButton.ORDER = 5;
PaintTerrainButton.prototype.IconSprite = "stretched:editor/toolbar/paintterrain.png";
PaintTerrainButton.prototype.Tooltip = translate("Paint terrain texture");
