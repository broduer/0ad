GameSettingControls.MapBrowser = class MapBrowser extends GameSettingControlButton
{
	constructor(...args)
	{
		super(...args);

		if (this.isSavedGame)
		{
			this.setHidden(true);
			return;
		}

		this.button.tooltip = colorizeHotkey(this.HotkeyTooltip, this.HotkeyConfig);
		this.button.hotkey = this.HotkeyConfig;
	}

	onSettingsLoaded()
	{
		if (this.gameSettingsController.guiData.lockSettings?.map)
		{
			this.setEnabled(false);
			this.setHidden(true);
			return;
		}
	}

	setControlHidden()
	{
		this.button.hidden = false;
	}

	onPress()
	{
		const page = this.setupWindow.pages.MapBrowserPage;

		if (!page.mapBrowserPage.hidden)
			page.submitMapSelection();
		else
			page.openPage(this.enabled);
	}
};

GameSettingControls.MapBrowser.prototype.HotkeyConfig =
	"mapbrowser";

GameSettingControls.MapBrowser.prototype.Caption =
	translate("Browse Maps");

GameSettingControls.MapBrowser.prototype.HotkeyTooltip =
	translate("%(hotkey)s: View the list of available maps.");
