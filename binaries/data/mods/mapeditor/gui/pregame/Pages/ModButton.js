export class ModButton
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.modButton = Engine.GetGUIObjectByName("modButton");
		this.modButton.caption = this.Caption;

		this.modButton.onPress = () =>
		{
			this.setupWindow.closePageCallback({ [Engine.openRequest]: {
				"page": "page_modmod.xml",
				"argument": {
					"cancelbutton": true
				}
			} });
		};
	}
}
ModButton.prototype.Caption = translate("Mods");
