import { EditorWindow } from "gui/editor2/EditorWindow.js";
export let getHotloadData;

export function init(initData, hotloadData)
{
	return new Promise((closePageCallback) =>
	{
		const editorWindow = new EditorWindow(initData, hotloadData, closePageCallback);

		globalThis.handleInputBeforeGui = (ev, hovered) =>
			editorWindow.onHandleInputBeforeGui(ev, hovered);
		globalThis.handleInputAfterGui = ev =>
			editorWindow.onHandleInputAfterGui(ev);

		getHotloadData = () => editorWindow.getHotloadData();
	});
}
