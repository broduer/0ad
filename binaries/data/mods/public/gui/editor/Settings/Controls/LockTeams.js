classEditorSettingControls.LockTeams = class extends EditorSettingControlCheckbox
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
			this.setupWindow.controls.editorSettings.editorPublicGeneral.watch(() => this.render(), ["lockTeams"]);
			this.render();
		});
	}

	render()
	{
		this.setChecked(this.setupWindow.controls.editorSettings.editorPublicGeneral.lockTeams);
	}

	getAutocompleteEntries()
	{
		return [];
	}

	onPress(checked)
	{
		this.setupWindow.controls.editorSettings.editorPublicGeneral.setLockTeams(checked);
	}
};

classEditorSettingControls.LockTeams.prototype.TitleCaption = translateWithContext("Map Editor", "Lock Teams");

classEditorSettingControls.LockTeams.prototype.Tooltip = translateWithContext("Map Editor", "If checked, teams will be locked");

classEditorSettingControls.LockTeams.prototype.AutocompleteOrder = 0;
