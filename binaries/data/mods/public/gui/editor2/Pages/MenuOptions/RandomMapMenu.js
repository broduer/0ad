import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";
import { StateNames } from "gui/editor2/Constants/ToolsAtlasController.js";

export class RandomMapMenu
{
	constructor(menu, button)
	{
		this.menu = menu;
		this.button = button;
		this.button.caption = this.Caption;
		this.setupWindow = menu.setupWindow;
		this.setupWindow.registerLoadHandler(()=>
		{
			this.gui = this.setupWindow.controllers[ControllerNames.GUI_CONTROLLER];
		});
	}

	onPress()
	{
		this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER].tool = "";
		this.setupWindow.controllers[ControllerNames.TOOLS_ATLAS_CONTROLLER].changeState(StateNames.IDLE);
		this.gui.closeAllUIExcept(GuiUISettings.RANDOM_MAP);
	}
}
RandomMapMenu.ORDER = 5;
RandomMapMenu.prototype.Caption = translate("Random Map");
