classEditorSettingControls.PlayerIndex = class extends EditorSettingControlDropdown
{
	constructor(...args)
	{
		super(...args);

		this.setupWindow.registerLoadHandler(() =>
		{
			this.setupWindow.controls.editorSettingsController.registerSettingsLoadedHandler(() =>
			{
				this.setup();
			});
			this.setupWindow.controls.editorSettings.editorPlayerGeneral.watch(() => this.setup(), ["playerNumber"]);
			this.setupWindow.controls.editorSettings.editorPlayerGeneral.watch(() => this.render(), ["playerIndex"]);
			this.setup();
		});
	}

	setup()
	{
		const listValues = Array.from({ "length": this.setupWindow.controls.editorSettings.editorPlayerGeneral.playerNumber }, (_, i)=> i + 1);
		this.dropdown.list = listValues;
		this.dropdown.list_data = listValues;

		this.render();
	}

	render()
	{
		this.setSelectedValue(this.setupWindow.controls.editorSettings.editorPlayerGeneral.playerIndex + 1);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onSelectionChange(value)
	{
		this.setupWindow.controls.editorSettings.editorPlayerGeneral.setPlayerIndex(+value);
	}
};

classEditorSettingControls.PlayerIndex.prototype.TitleCaption = translateWithContext("Map Editor", "Player Index");

classEditorSettingControls.PlayerIndex.prototype.Tooltip = translateWithContext("Map Editor", "Current player for change settings");

classEditorSettingControls.PlayerIndex.prototype.AutocompleteOrder = 0;
