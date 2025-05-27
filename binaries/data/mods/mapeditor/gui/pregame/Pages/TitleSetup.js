export class TitleSetup
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.title = Engine.GetGUIObjectByName("setupWindowTitle");
		this.title.caption = this.Caption;
	}
}
TitleSetup.prototype.Caption = translate("Menu Options");
