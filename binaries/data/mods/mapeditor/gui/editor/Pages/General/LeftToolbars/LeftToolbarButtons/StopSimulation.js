leftToolbarButtons.Toolbars.StopSimulationButton = class
{
	constructor(toolbar, button, icon)
	{
		this.button = button;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		icon.sprite = this.IconSprite;

		this.setupWindow.registerLoadHandler(() =>
		{
			this.button.hidden = true;
			this.setupWindow.controls.simulationController.watch(() =>
			{
				this.button.hidden = !this.setupWindow.controls.simulationController.isStarted;
			}, ['isStarted']);
		});
	}

	onPress()
	{
		if (!this.setupWindow.controls.simulationController.isStarted)
			return;

		this.setupWindow.controls.simulationController.StopSimulation();
	}
};

leftToolbarButtons.Toolbars.StopSimulationButton.prototype.IconSprite = "stretched:editor/simulation/stop.png";
leftToolbarButtons.Toolbars.StopSimulationButton.prototype.Tooltip = translateWithContext("Map Editor", "Stop simulation");
leftToolbarButtons.Toolbars.StopSimulationButton.ORDER = 2;
