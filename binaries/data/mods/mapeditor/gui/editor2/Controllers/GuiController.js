import { Observable } from "gui/global/Observable.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";

export class GuiController extends Observable
{
	constructor(setupWindow)
	{
		super();
		this.setupWindow = setupWindow;

		for (const setting in GuiUISettings)
			this[GuiUISettings[setting] + "IsOpen"] = false;

		this.mapChange = false;
		this.currentState = "";
		this.currentTerrainTexture = this.DefaultTerrainTexture;
	}

	closeAllUIExcept(exception)
	{
		for (const setting in GuiUISettings)
		{
			const key = GuiUISettings[setting] + "IsOpen";
			this[key] = false;
		}
		if (exception && Object.prototype.hasOwnProperty.call(this, exception + "IsOpen"))
			this[exception + "IsOpen"] = true;
	}
}

GuiController.prototype.DefaultTerrainTexture = "editor";
