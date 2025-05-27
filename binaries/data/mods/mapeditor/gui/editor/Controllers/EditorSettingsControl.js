EditorWindow.prototype.ClassControls.EditorSettings = class extends EditorSettings
{
	constructor(setupWindow)
	{
		super();

		this.setupWindow = setupWindow;

		this.setupWindow.registerLoadHandler(() =>
		{
			this.init(this.setupWindow.controls.mapEditorCache);
		});
	}
};

EditorWindow.prototype.ClassControls.EditorSettings.ORDER = 1;
