classEditorSettingControls.PlayerAI = class extends EditorSettingControlDropdown
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

			this.setupWindow.controls.editorSettings.editorPlayerGeneral.watch(() => this.render(), ["currentAI"]);
			this.setup();
		});
	}

	setup()
	{
		const listValues = MapEditor.GetAIData();
		this.dropdown.list = ["None"].concat(listValues.map(x => x.data.name));
		this.dropdown.list_data = ["none"].concat(listValues.map(x => x.id));

		this.render();
	}

	render()
	{
		this.setSelectedValue(this.setupWindow.controls.editorSettings.editorPlayerGeneral.currentAI || "none");
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onSelectionChange(itemIdx)
	{
		this.setupWindow.controls.editorSettings.editorPlayerGeneral.setCurrentAI(this.dropdown.list_data[itemIdx]);
	}
};

classEditorSettingControls.PlayerAI.prototype.TitleCaption = translateWithContext("Map Editor", "AI");

classEditorSettingControls.PlayerAI.prototype.Tooltip = translateWithContext("Map Editor", "Set player AI");

classEditorSettingControls.PlayerAI.prototype.AutocompleteOrder = 0;
