toolbarButtons.Toolbars.TransformPathButton = class
{
	constructor(toolbar, button, icon)
	{
		this.button = button;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		icon.sprite = this.IconSprite;
	}

	onToogle(isToogle)
	{
		warn(`${this.button.name} ${isToogle}`);
	}
};

toolbarButtons.Toolbars.TransformPathButton.ORDER = 6;
toolbarButtons.Toolbars.TransformPathButton.prototype.IconSprite = "stretched:editor/toolbar/movepath.png";
toolbarButtons.Toolbars.TransformPathButton.prototype.Tooltip = translate("Move cinema path nodes");
