EditorWindow.prototype.ClassControls.SimulationController = class extends ObservableSetting
{
	constructor(setupWindow)
	{
		super();
		this.setupWindow = setupWindow;

		this.isPlaying = false;
		this.isStarted = false;
	}

	StartSimulation()
	{
		if (this.isStarted)
		{
			warn("Simulation already started");
			return;
		}

		if (MapEditor.PlaySimulation())
		{
			this.isStarted = true;
			this.isPlaying = true;
		}
		else
			warn("Simulation failed to start");
	}

	StopSimulation()
	{
		if (!this.isStarted)
		{
			warn("Simulation not started");
			return;
		}

		if (MapEditor.StopSimulation())
		{
			this.isStarted = false;
			this.isPlaying = false;
		}
		else
			warn("Simulation failed to stop");
	}

	ResumeOrPauseSimulation()
	{
		if (!this.isStarted)
		{
			warn("Simulation not started");
			return;
		}

		MapEditor.ResumeOrPauseSimulation();
		this.isPlaying = !this.isPlaying;
	}
};
