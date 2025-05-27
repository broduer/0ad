export class ExitMenu
{
	constructor(menu, button)
	{
		this.menu = menu;
		this.button = button;
		this.button.caption = this.Caption;
		this.setupWindow = menu.setupWindow;
	}

	onPress()
	{
		this.setupWindow.closePage();
	}
}

ExitMenu.ORDER = 1000;
ExitMenu.prototype.Caption = translate("Exit");
