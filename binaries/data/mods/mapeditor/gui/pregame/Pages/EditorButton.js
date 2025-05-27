export class EditorButton
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.editorButton = Engine.GetGUIObjectByName("editorButton");
		this.editorButton.caption = this.Caption;

		this.editorButton.onPress = () =>
		{
			this.setupWindow.closePageCallback({ [Engine.openRequest]: {
				"page": "page_editor.xml"
			} });
		};
	}
}
EditorButton.prototype.Caption = translate("Editor");
