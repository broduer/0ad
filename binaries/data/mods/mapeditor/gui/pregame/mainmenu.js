import { MainMenuWindow } from "gui/pregame/MainMenuWindow.js";
export let getHotloadData;

export function init(initData, hotloadData)
{
	return new Promise((closePageCallback) =>
	{
		const menuWindow = new MainMenuWindow(initData, hotloadData, closePageCallback);

		globalThis.handleInputBeforeGui = (ev, hovered) =>
			menuWindow.onHandleInputBeforeGui(ev, hovered);
		globalThis.handleInputAfterGui = ev =>
			menuWindow.onHandleInputAfterGui(ev);

		getHotloadData = () => menuWindow.getHotloadData();
	});
}
