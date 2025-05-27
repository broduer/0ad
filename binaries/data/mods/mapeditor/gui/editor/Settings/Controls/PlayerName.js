classEditorSettingControls.PlayerName = class extends EditorSettingControlTextbox
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
			this.setupWindow.controls.editorSettings.editorPlayerGeneral.watch(() => this.render(), ["currentName"]);
			this.render();
		});
	}

	render()
	{
		this.setValue(this.setupWindow.controls.editorSettings.editorPlayerGeneral.currentName);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onTextEdit()
	{
		if (!this.isInGuiUpdate)
			this.setupWindow.controls.editorSettings.editorPlayerGeneral.setCurrentName(this.input.caption);
	}
};

classEditorSettingControls.PlayerName.prototype.TitleCaption = translateWithContext("Map Editor", "Player Name");

classEditorSettingControls.PlayerName.prototype.Tooltip = translateWithContext("Map Editor", "Set the player Name");

classEditorSettingControls.PlayerName.prototype.AutocompleteOrder = 0;
