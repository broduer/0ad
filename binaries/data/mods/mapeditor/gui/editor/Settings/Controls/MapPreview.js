classEditorSettingControls.MapPreview = class extends EditorSettingControlTextbox
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
			this.setupWindow.controls.editorSettings.editorMapGeneral.watch(() => this.render(), ["preview"]);
			this.render();
		});
	}

	render()
	{
		this.setValue(this.setupWindow.controls.editorSettings.editorMapGeneral.preview);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onTextEdit()
	{
		if (!this.isInGuiUpdate)
			this.setupWindow.controls.editorSettings.editorMapGeneral.setPreview(this.input.caption);
	}
};

classEditorSettingControls.MapPreview.prototype.TitleCaption = translateWithContext("Map Editor", "Map Preview");

classEditorSettingControls.MapPreview.prototype.Tooltip = translateWithContext("Map Editor", "Set the preview of your map");

classEditorSettingControls.MapPreview.prototype.AutocompleteOrder = 0;
