classEditorSettingControls.AllyView = class extends EditorSettingControlCheckbox
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
			this.setupWindow.controls.editorSettings.editorPublicGeneral.watch(() => this.render(), ["allyView"]);
			this.render();
		});
	}

	render()
	{
		this.setChecked(this.setupWindow.controls.editorSettings.editorPublicGeneral.allyView);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onPress(checked)
	{
		this.setupWindow.controls.editorSettings.editorPublicGeneral.setAllyView(checked);
	}
};

classEditorSettingControls.AllyView.prototype.TitleCaption = translateWithContext("Map Editor", "Ally view");

classEditorSettingControls.AllyView.prototype.Tooltip = translateWithContext("Map Editor", "If checked, players will be able to see what their teammates see and won't need to research cartography");

classEditorSettingControls.AllyView.prototype.AutocompleteOrder = 0;
