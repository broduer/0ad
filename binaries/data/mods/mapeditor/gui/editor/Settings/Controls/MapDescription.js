classEditorSettingControls.MapDescription = class extends EditorSettingControlTextbox
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
			this.setupWindow.controls.editorSettings.editorMapGeneral.watch(() => this.render(), ["description"]);
			this.render();
		});
	}

	render()
	{
		this.setValue(this.setupWindow.controls.editorSettings.editorMapGeneral.description);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onTextEdit()
	{
		if (!this.isInGuiUpdate)
			this.setupWindow.controls.editorSettings.editorMapGeneral.setDescription(this.input.caption);
	}
};

classEditorSettingControls.MapDescription.prototype.TitleCaption = translateWithContext("Map Editor", "Map Description");

classEditorSettingControls.MapDescription.prototype.Tooltip = translateWithContext("Map Editor", "Set the description of your map");

classEditorSettingControls.MapDescription.prototype.AutocompleteOrder = 0;
