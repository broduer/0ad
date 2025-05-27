classEditorSettingControls.RevealMap = class extends EditorSettingControlCheckbox
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
			this.setupWindow.controls.editorSettings.editorMapGeneral.watch(() => this.render(), ["revealMap"]);
			this.render();
		});
	}

	render()
	{
		this.setChecked(this.setupWindow.controls.editorSettings.editorMapGeneral.revealMap);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onPress(checked)
	{
		this.setupWindow.controls.editorSettings.editorMapGeneral.setRevealMap(checked);
	}
};

classEditorSettingControls.RevealMap.prototype.TitleCaption = translateWithContext("Map Editor", "Reveal Map");

classEditorSettingControls.RevealMap.prototype.Tooltip = translateWithContext("Map Editor", "Set if map needs to be explore at the beggining");

classEditorSettingControls.RevealMap.prototype.AutocompleteOrder = 0;
