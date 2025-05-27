classEditorSettingControls.MapName = class extends EditorSettingControlTextbox
{
	constructor(...args)
	{
		super(...args);

		this.setupWindow.registerLoadHandler(() =>
		{
			this.setupWindow.controls.editorSettingsController.registerSettingsLoadedHandler(() =>
			{
				this.render();
			});
			this.setupWindow.controls.editorSettings.editorMapGeneral.watch(() => this.render(), ["name"]);
			this.render();
		});
	}

	render()
	{
		this.setValue(this.setupWindow.controls.editorSettings.editorMapGeneral.name);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onTextEdit()
	{
		if (!this.isInGuiUpdate)
			this.setupWindow.controls.editorSettings.editorMapGeneral.setName(this.input.caption);
	}
};

classEditorSettingControls.MapName.prototype.TitleCaption = translateWithContext("Map Editor", "Map Name");

classEditorSettingControls.MapName.prototype.Tooltip = translateWithContext("Map Editor", "Set the name of your map");

classEditorSettingControls.MapName.prototype.AutocompleteOrder = 0;
