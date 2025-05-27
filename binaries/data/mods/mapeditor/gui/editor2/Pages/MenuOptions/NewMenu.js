import { Provenances } from "gui/editor2/Constants/SessionController.js";

export class NewMenu
{
	constructor(menu, button)
	{
		this.menu = menu;
		this.button = button;
		this.button.caption = this.Caption;
		this.setupWindow = menu.setupWindow;
		this.setupWindow.registerLoadHandler(()=>
		{
			this.onPress();
		});
	}
	onPress()
	{
		this.setupWindow.controllers.sessionController.transition({
			"mapType": this.MapType,
			"map": this.Map
		}, Provenances.EMPTY);
	}
}
NewMenu.ORDER = 0;
NewMenu.prototype.Caption = translate("New");
NewMenu.prototype.MapType = "scenario";
NewMenu.prototype.Map = "maps/scenarios/_editor";
