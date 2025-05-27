toolbarButtons.Toolbars.TransformButton = class
{
	constructor(toolbar, button, icon, index)
	{
		this.button = button;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		icon.sprite = this.IconSprite;

		this.index = index;
		this.stateName = "selector";
		this.isToogle = false;

		this.setupWindow.controls.guiController.watch(()=>
		{
			if (this.isToogle && this.setupWindow.controls.guiController.currentState !== this.stateName)
				toolbar.updateToogleWithoutEvent(this.index, this.isToogle = false);
			else if (!this.isToogle && this.setupWindow.controls.guiController.currentState === this.stateName)
				toolbar.updateToogleWithoutEvent(this.index, this.isToogle = true);
		}, ["currentState"]);
	}

	onToogle(isToogle)
	{
		if (isToogle)
			this.setupWindow.controls.toolsAtlasController.changeState("selector");

		this.isToogle = isToogle;
	}
};

toolbarButtons.Toolbars.TransformButton.ORDER = 1;
toolbarButtons.Toolbars.TransformButton.prototype.IconSprite = "stretched:editor/toolbar/moveobject.png";
toolbarButtons.Toolbars.TransformButton.prototype.Tooltip = translate("Move/rotate object");
