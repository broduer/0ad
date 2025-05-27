leftToolbarButtons.Toolbars.PlaySimulationButton = class
{
	constructor(toolbar, button, icon)
	{
		this.button = button;
		this.icon = icon;
		this.setupWindow = toolbar.setupWindow;
		this.button.tooltip = this.Tooltip;
		this.icon.sprite = this.PlaySprite;

		this.setupWindow.registerLoadHandler(() =>
		{
			this.setupWindow.controls.simulationController.watch(() =>
			{
				this.icon.sprite = this.setupWindow.controls.simulationController.isPlaying ? this.PauseSprite : this.PlaySprite;
			}, ['isPlaying']);
		});
	}

	onPress()
	{
		if (!this.setupWindow.controls.simulationController.isStarted)
			this.setupWindow.controls.simulationController.StartSimulation();
		else
			this.setupWindow.controls.simulationController.ResumeOrPauseSimulation();
	}
};

leftToolbarButtons.Toolbars.PlaySimulationButton.prototype.PlaySprite = "stretched:editor/simulation/play.png";
leftToolbarButtons.Toolbars.PlaySimulationButton.prototype.PauseSprite = "stretched:editor/simulation/pause.png";
leftToolbarButtons.Toolbars.PlaySimulationButton.prototype.Tooltip = translateWithContext("Map Editor", "Play/Pause simulation");
leftToolbarButtons.Toolbars.PlaySimulationButton.ORDER = 1;
