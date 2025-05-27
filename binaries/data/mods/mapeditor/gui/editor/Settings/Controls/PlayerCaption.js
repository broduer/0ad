classEditorSettingControls.PlayerCaption = class extends EditorSettingControlCaption
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
			this.setupWindow.controls.editorSettings.editorPlayerGeneral.watch(() => this.render(), ["playerIndex"]);
			this.render();
		});
	}

	render()
	{
		this.title.caption = sprintf(`${this.TitleCaption} (%(number)s)`, {
			"number": this.setupWindow.controls.editorSettings.editorPlayerGeneral.playerIndex + 1
		});
	}

	getAutocompleteEntries()
	{
		return [];
	}
};

classEditorSettingControls.PlayerCaption.prototype.TitleCaption = translateWithContext("Map Editor", "Player Settings");

classEditorSettingControls.PlayerCaption.prototype.Tooltip = translateWithContext("Map Editor", "Set the name of your map");

classEditorSettingControls.PlayerCaption.prototype.AutocompleteOrder = 0;
