EditorWindow.prototype.ClassControls.ObjectSettings = class extends ObservableSetting
{
	constructor(setupWindow)
	{
		super();
		this.tool = "";
		this.option = "";
		this.angle = this.DefaultAngle;
		this.playerId = 0; // gaia player
	}
};

EditorWindow.prototype.ClassControls.ObjectSettings.prototype.DefaultAngle = Math.PI * 3.0 / 4.0;
