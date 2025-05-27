import { BaseSetupWindow } from "gui/global/BaseSetupWindow.js";
import { controllers } from "gui/editor2/Controllers/index.js";
import { pages } from "gui/editor2/Pages/index.js";

export class EditorWindow extends BaseSetupWindow
{
	constructor(initData, hotloadData, closePageCallback)
	{
		super(initData, hotloadData, closePageCallback, controllers, pages);
	}
}
