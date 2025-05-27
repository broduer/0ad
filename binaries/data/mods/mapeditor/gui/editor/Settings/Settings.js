EditorWindow.prototype.ClassSetupWindowPages.SettingsPage = class
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;

		this.gui = Engine.GetGUIObjectByName("settingsWindow");
		this.title = Engine.GetGUIObjectByName("settigsWindowTitle");
		this.save = Engine.GetGUIObjectByName("settingsWindowSaveButton");
		this.cancel = Engine.GetGUIObjectByName("settingsWindowCancelButton");
		this.settingsControls = Engine.GetGUIObjectByName("settingsWindowControls");

		this.tabSelectedIndex = -1;

		// set default titles
		this.title.caption = this.TitleCaption;
		this.save.caption = this.SaveCaption;
		this.save.tooltip = this.SaveTooltip;
		this.cancel.caption = this.CancelCaption;
		this.cancel.tooltip = this.CancelTooltip;

		this.cancel.onPress = () =>
		{
			this.setupWindow.controls.guiController.mapSettingsIsOpen = false;
		};

		this.save.onPress = () => this.onSave();

		this.settingsControls.onWindowResized = this.repositionSettings.bind(this);

		this.setupWindow.registerLoadHandler(() =>
		{
			this.editorSettingControlManager = this.setupWindow.controls.editorSettingsControllerManager;
			this.setupWindow.controls.guiController.watch(() => this.onMapSettingsIsOpenChange(), ["mapSettingsIsOpen"]);
			this.setupSettingsUI();
		});
	}

	onSave()
	{
		// we need to find a way to validate that current configuration is valid
		this.setupWindow.controls.editorSettingsController.saveMapSettings();
		this.setupWindow.controls.guiController.mapSettingsIsOpen = false;
	}

	setupSettingsUI()
	{
		for (const settingOptionIndex in this.SettingsOptions)
		{
			const button = Engine.GetGUIObjectByName("settingsWindowTabButton[" + settingOptionIndex + "]");
			if (!button)
			{
				warn("Too few tab-buttons!");
				break;
			}

			button.hidden = false;

			const size = button.size;
			size.top = settingOptionIndex * (this.TabButtonSize + this.TabButtonSpacing) + this.TabButtonSpacing / 2;
			size.bottom = size.top + this.TabButtonSize;
			size.rbottom = 0;

			button.size = size;
			button.onPress = () => this.onTabSelected(settingOptionIndex);
			Engine.GetGUIObjectByName("settingsWindowTabButtonText[" + settingOptionIndex + "]").caption = this.SettingsOptions[settingOptionIndex].label;
		}
		this.onTabSelected(0);
	}

	onTabSelected(newTabSelectedIndex)
	{
		if (newTabSelectedIndex != this.tabSelectedIndex)
		{
			if (this.tabSelectedIndex >= 0)
				Engine.GetGUIObjectByName("settingsWindowTabButton[" + this.tabSelectedIndex + "]").sprite = "ModernTabVerticalBackground";
			Engine.GetGUIObjectByName("settingsWindowTabButton[" + newTabSelectedIndex + "]").sprite = "ModernTabVerticalForeground";
			this.tabSelectedIndex = newTabSelectedIndex;

			this.updateVisibilitySettings();
		}
	}

	updateVisibilitySettings()
	{
		const settings = this.SettingsOptions[this.tabSelectedIndex].settings;
		this.editorSettingControlManager.hideSettingByName((settingName)=> !settings.includes(settingName));
		this.repositionSettings();
	}

	repositionSettings()
	{
		const settings = this.SettingsOptions[this.tabSelectedIndex].settings;
		const size = this.settingsControls.getComputedSize();

		let yPos = this.SettingSpacing;

		for (const settingName of settings)
		{
			const frame = this.editorSettingControlManager.editorSettingControls[settingName].frame;
			if (frame.hidden)
				continue;

			frame.size = {
				"left": 16,
				"top": yPos,
				"right": (size.right - size.left) - 16,
				"bottom": yPos + this.SettingHeight
			};

			yPos = yPos + this.SettingHeight + this.SettingSpacing;
		}
	}

	onMapSettingsIsOpenChange()
	{
		const newValue = this.setupWindow.controls.guiController.mapSettingsIsOpen;
		if (this.gui.hidden && newValue)
			this.setupWindow.controls.editorSettingsController.loadMapSettings();

		this.gui.hidden = !newValue;
	}
};

EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.SaveCaption = translate("Save");
EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.SaveTooltip = translate("Save");

EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.CancelCaption = translate("Close");
EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.CancelTooltip = translate("Close");

EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.TitleCaption = translate("Map Settings");

EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.TabButtonSize = 36;
EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.TabButtonSpacing = 4;

EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.SettingHeight = 34;
EditorWindow.prototype.ClassSetupWindowPages.SettingsPage.prototype.SettingSpacing = 4;
