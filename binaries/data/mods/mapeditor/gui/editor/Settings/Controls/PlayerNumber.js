classEditorSettingControls.PlayerNumber = class extends EditorSettingControlDropdown
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
			this.setup();
		});
	}

	setup()
	{
		const listValues = Array.from({ "length": this.setupWindow.controls.editorSettings.editorPlayerGeneral.maxPlayer }, (_, i)=> i + 1);
		this.dropdown.list = listValues;
		this.dropdown.list_data = listValues;

		this.render();
	}

	render()
	{
		this.setSelectedValue(this.setupWindow.controls.editorSettings.editorPlayerGeneral.playerNumber);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onSelectionChange(value)
	{
		this.setupWindow.controls.editorSettings.editorPlayerGeneral.setPlayerNumber((+value) + 1);
	}
};

classEditorSettingControls.PlayerNumber.prototype.TitleCaption = translateWithContext("Map Editor", "Player Number");

classEditorSettingControls.PlayerNumber.prototype.Tooltip = translateWithContext("Map Editor", "Set maximum players");

classEditorSettingControls.PlayerNumber.prototype.AutocompleteOrder = 0;
