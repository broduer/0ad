classEditorSettingControls.CircularMap = class extends EditorSettingControlCheckbox
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
			this.setupWindow.controls.editorSettings.editorMapGeneral.watch(() => this.render(), ["circularMap"]);
			this.render();
		});
	}

	render()
	{
		this.setChecked(this.setupWindow.controls.editorSettings.editorMapGeneral.circularMap);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onPress(checked)
	{
		this.setupWindow.controls.editorSettings.editorMapGeneral.setCircularMap(checked);
	}
};

classEditorSettingControls.CircularMap.prototype.TitleCaption = translateWithContext("Map Editor", "Circular Map");

classEditorSettingControls.CircularMap.prototype.Tooltip = translateWithContext("Map Editor", "Set the shape of your map in circular way");

classEditorSettingControls.CircularMap.prototype.AutocompleteOrder = 0;
