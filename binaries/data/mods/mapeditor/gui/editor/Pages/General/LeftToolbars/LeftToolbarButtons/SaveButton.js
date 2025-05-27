leftToolbarButtons.Toolbars.SaveButton = class
{
	constructor(toolbar, button, icon)
	{
		this.button = button;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		icon.sprite = this.IconSprite;
	}

	onPress()
	{
		this.setupWindow.controls.guiController.saveSettingsIsOpen = true;
	}
};

leftToolbarButtons.Toolbars.SaveButton.prototype.IconSprite = "stretched:editor/lefttoolbar/save.png";
leftToolbarButtons.Toolbars.SaveButton.prototype.Tooltip = translateWithContext("Map Editor", "Save");
