import { BaseSetupWindow } from "gui/global/BaseSetupWindow.js";
import { PregameClassSetupWindowPages } from "gui/pregame/Pages/index.js";

export class MainMenuWindow extends BaseSetupWindow
{
	constructor(initData, hotloadData, closePageCallback)
	{
		super(initData, hotloadData, closePageCallback, null, PregameClassSetupWindowPages);
	}
}
