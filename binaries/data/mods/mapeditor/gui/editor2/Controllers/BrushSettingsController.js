import { Observable } from "gui/global/Observable.js";
import { InfoSettings } from "gui/editor2/Constants/BrushSettingsController.js";

export class BrushSettingsController extends Observable
{
	constructor()
	{
		super();
		this.tool = "";
		this.info = {
			[InfoSettings.SIZE]: 1,
			[InfoSettings.DATA]: []
		};
		this.strength = 1;
	}
}
