classEditorSettingControls.PlayerCiv = class extends EditorSettingControlDropdown
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

			this.setupWindow.controls.editorSettings.editorPlayerGeneral.watch(() => this.render(), ["currentCiv"]);
			this.setup();
		});
	}

	setup()
	{
		const listValues = MapEditor.GetCivData();
		this.dropdown.list = listValues.map(x => x[1]);
		this.dropdown.list_data = listValues.map(x => x[0]);

		this.render();
	}

	render()
	{
		this.setSelectedValue(this.setupWindow.controls.editorSettings.editorPlayerGeneral.currentCiv);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onSelectionChange(itemIdx)
	{
		this.setupWindow.controls.editorSettings.editorPlayerGeneral.setCurrentCiv(this.dropdown.list_data[itemIdx]);
	}
};

classEditorSettingControls.PlayerCiv.prototype.TitleCaption = translateWithContext("Map Editor", "Player Civilization");

classEditorSettingControls.PlayerCiv.prototype.Tooltip = translateWithContext("Map Editor", "Set player civilization");

classEditorSettingControls.PlayerCiv.prototype.AutocompleteOrder = 0;
