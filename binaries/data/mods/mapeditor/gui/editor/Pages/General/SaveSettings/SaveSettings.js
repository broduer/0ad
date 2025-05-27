EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage = class
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;

		this.gui = Engine.GetGUIObjectByName("saveSettingsWindow");
		this.title = Engine.GetGUIObjectByName("saveSettigsWindowTitle");
		this.save = Engine.GetGUIObjectByName("saveSettingsWindowSaveButton");
		this.cancel = Engine.GetGUIObjectByName("saveSettingsWindowCancelButton");
		this.mapFolderLabel = Engine.GetGUIObjectByName("mapFolderLabel");
		this.mapFolderCtrl = Engine.GetGUIObjectByName("mapFolderCtrl");
		this.mapNameLabel = Engine.GetGUIObjectByName("mapNameLabel");
		this.mapNameCtrl = Engine.GetGUIObjectByName("mapNameCtrl");

		// set default titles
		this.title.caption = this.TitleCaption;
		this.save.caption = this.SaveCaption;
		this.save.tooltip = this.SaveTooltip;
		this.cancel.caption = this.CancelCaption;
		this.cancel.tooltip = this.CancelTooltip;
		this.mapFolderLabel.caption = this.MapFolderCaption;
		this.mapNameLabel.caption = this.MapNameCaption;

		this.cancel.onPress = () =>
		{
			this.setupWindow.controls.guiController.saveSettingsIsOpen = false;
		};

		this.save.onPress = () =>
		{
			const mapTypeInfo = this.setupWindow.controls.editorSettings.Helpers.MapTypes[this.mapFolderCtrl.selected];
			const mapNameFiltered = this.mapNameCtrl.caption.replace(/([^A-Za-z]+)/g, '').trim();

			if (mapNameFiltered.length === 0)
			{
				warn('please specify a valid name for map');
				return;
			}

			const mapPath = `${mapTypeInfo.Path}${mapNameFiltered}.xml`;

			this.setupWindow.controls.saveControlManager.onSaveMapInfo({
				"map": mapPath,
				"mapType": mapTypeInfo.Name
			});
		};

		this.setupWindow.registerLoadHandler(() =>
		{
			this.setupWindow.controls.guiController.watch(() => this.onSaveSettingsIsOpenChange(), ["saveSettingsIsOpen"]);

			// Load mapType (Remove Random maps)
			this.mapFolderCtrl.list = this.setupWindow.controls.editorSettings.Helpers.MapTypes.slice(0, -1).map(o => o.Title);
			this.mapFolderCtrl.list_data = this.setupWindow.controls.editorSettings.Helpers.MapTypes.slice(0, -1).map(o => o.Name);
			this.mapFolderCtrl.selected = 0;
		});
	}

	onSaveSettingsIsOpenChange()
	{
		const newValue = this.setupWindow.controls.guiController.saveSettingsIsOpen;
		if (newValue)
		{
			const saveMapInfo = this.setupWindow.controls.saveControlManager.getSaveMapInfo();
			if (saveMapInfo.editorType === 'new')
			{
				this.mapNameCtrl.caption = '';
				this.mapFolderCtrl.selected = 0;
			}
			else
			{
				const mapTypeInfo = this.setupWindow.controls.editorSettings.Helpers.MapTypes.find(m => m.Name === saveMapInfo.mapType);

				this.mapFolderCtrl.selected = this.mapFolderCtrl.list_data.indexOf(saveMapInfo.mapType);
				this.mapNameCtrl.caption = saveMapInfo.map.replace(mapTypeInfo.Path, '').replace('.xml', '');
			}
		}
		this.gui.hidden = !newValue;
	}
};

EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.SaveCaption = translateWithContext("Map Editor", "Save");
EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.SaveTooltip = translateWithContext("Map Editor", "Save");

EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.CancelCaption = translateWithContext("Map Editor", "Close");
EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.CancelTooltip = translateWithContext("Map Editor", "Close");

EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.TitleCaption = translateWithContext("Map Editor", "Save Settings");

EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.MapFolderCaption = translateWithContext("Map Editor", "Map Folder");
EditorWindow.prototype.ClassSetupWindowPages.SaveSettingsPage.prototype.MapNameCaption = translateWithContext("Map Editor", "Map Name");
