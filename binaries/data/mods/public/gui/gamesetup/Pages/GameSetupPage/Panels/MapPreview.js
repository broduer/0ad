class MapPreview
{
	constructor(setupWindow, isSavedGame)
	{
		this.setupWindow = setupWindow;
		this.gameSettingsController = setupWindow.controls.gameSettingsController;
		this.mapCache = setupWindow.controls.mapCache;

		this.mapInfoName = Engine.GetGUIObjectByName("mapInfoName");
		this.mapPreview = Engine.GetGUIObjectByName("mapPreview");
		this.missingMapButton = Engine.GetGUIObjectByName("missingMapButton");
		this.missingMapButton.onPress = this.onMissingMapPress.bind(this);
		this.missingMapButton.tooltip = this.MissingMapTooltip;

		// Delay the settings registration handler until we have the map cache.
		setupWindow.controls.gameSettingsController.registerSettingsLoadedHandler(
			this.onSettingsLoaded.bind(this));

		if (!isSavedGame)
		{
			// TODO: Why does onPress not work? CGUI.cpp seems to support it.
			this.mapPreview.onMouseLeftPress = this.onPress.bind(this);
		}
		this.mapPreview.tooltip = this.Tooltip;
	}

	onSettingsLoaded()
	{
		this.renderName();
		this.renderPreview();
		g_GameSettings.map.watch(() => this.renderName(), ["map"]);
		g_GameSettings.mapName.watch(() => this.renderName(), ["value"]);
		g_GameSettings.mapPreview.watch(() => this.renderPreview(), ["value"]);
	}

	onPress()
	{
		this.setupWindow.pages.MapBrowserPage.openPage(true);
	}

	async onMissingMapPress()
	{
		const modsBefore = Object.keys(Engine.GetAvailableMods());
		if (!await openModIo({ "filter": this.getMissingMapName() }))
			return;

		if (Object.keys(Engine.GetAvailableMods()).every(mod => modsBefore.includes(mod)))
			return;

		messageBox(
			500, 200,
			translate("To use a downloaded mod, enable it in Settings → Mod Selection and restart the game. Restarting leaves this match."),
			translate("Mod Downloaded"));
	}

	/**
	 * The host sends the map's display name. Fall back to the file name if there is none.
	 */
	getMissingMapName()
	{
		const mapName = g_GameSettings.mapName.value || g_GameSettings.map.map;
		return mapName.includes("/") ? mapName.split("/").pop().replace(/_/g, " ") : mapName;
	}

	renderName()
	{
		const isMissing = !!g_GameSettings.map.map &&
			!this.mapCache.checkIfExists(g_GameSettings.map.type, g_GameSettings.map.map);
		this.missingMapButton.hidden = !isMissing;

		if (!g_GameSettings.map.map)
		{
			this.mapInfoName.caption = translate("No selected map");
			return;
		}

		if (isMissing)
		{
			this.mapInfoName.caption = sprintf(translate("%(mapName)s (not installed)"), {
				"mapName": this.getMissingMapName()
			});
			return;
		}

		this.mapInfoName.caption = this.mapCache.translateMapName(
			this.mapCache.getTranslatableMapName(g_GameSettings.map.type, g_GameSettings.map.map));
	}

	renderPreview()
	{
		if (!g_GameSettings.mapPreview.value)
		{
			this.mapPreview.sprite = this.mapCache.getMapPreview();
			return;
		}
		this.mapPreview.sprite = g_GameSettings.mapPreview.value;
	}
}

MapPreview.prototype.Tooltip =
	translate("Click to view the list of available maps.");

MapPreview.prototype.MissingMapTooltip =
	translate("This map is not installed. Search mod.io for a mod that adds it.");
