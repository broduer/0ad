import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";
import { Provenances } from "gui/editor2/Constants/SessionController.js";

export class RandomMapPage
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.guiRadomMap = Engine.GetGUIObjectByName("randomMap");
		this.rmScript = Engine.GetGUIObjectByName("rmScript");
		this.rmGenerate = Engine.GetGUIObjectByName("rmGenerate");
		this.rmGenerate.caption = this.GenerateCaption;
		this.rmSize = Engine.GetGUIObjectByName("rmSize");
		this.rmSeed = Engine.GetGUIObjectByName("rmSeed");
		this.rmReroll = Engine.GetGUIObjectByName("rmReroll");
		this.rmReroll.caption = this.RerollCaption;
		this.rmBiome = Engine.GetGUIObjectByName("rmBiome");
		this.rmPlayers = Engine.GetGUIObjectByName("rmPlayers");
		this.rmPlacements = Engine.GetGUIObjectByName("rmPlacements");

		this.seed = 0;
		this.rmSeed.caption = this.seed.toString();

		this.setupWindow.registerLoadHandler(()=>
		{
			this.gui = this.setupWindow.controllers[ControllerNames.GUI_CONTROLLER];
			this.session = this.setupWindow.controllers[ControllerNames.SESSION_CONTROLLER];
			this.atlas = this.setupWindow.controllers[ControllerNames.TOOLS_ATLAS_CONTROLLER];
			this.brush = this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER];
			this.gui.watch(() =>
			{
				this.guiRadomMap.hidden = false;
			}, [GuiUISettings.RANDOM_MAP + "IsOpen"]); // TODO: Replace harcoded IsOpen

			this.loadRandomMaps();
			this.rmGenerate.onPress = this.onGenerate.bind(this);
			this.rmReroll.onPress = this.onReroll.bind(this);
			this.rmSeed.onTextEdit = this.onSeedChanged.bind(this);

			this.brush.watch(() =>
			{
				if (this.brush.tool != "")
					this.guiRadomMap.hidden = true;
			}, ["tool"]); // TODO: Replace harcoded IsOpen
		});
	}

	onReroll()
	{
		this.seed = Math.floor(Math.random() * 1000000);
		this.rmSeed.caption = this.seed.toString();
	}

	onSeedChanged()
	{
		const seed = parseInt(this.rmSeed.caption);
		if (!isNaN(seed))
			this.seed = seed;
		else
			this.rmSeed.caption = this.seed.toString();
	}

	loadRandomMaps()
	{
		const files = Engine.ListDirectoryFiles(this.RandomMapPath, this.RandomMapExtension, false);
		this.randomMaps = [];
		for (const file of files)
		{
			const mapData = Engine.ReadJSONFile(file);
			if (!mapData || !mapData.settings)
				continue;
			this.randomMaps.push({ "file": mapData.settings.Script, "data": mapData });
		}

		this.rmScript.onSelectionChange = () =>
		{
			const biomes = [];

			if (this.rmScript.selected >= 0 && this.randomMaps.length > 0)
			{
				const selectedMap = this.randomMaps[this.rmScript.selected];
				if (selectedMap && selectedMap.data && selectedMap.data.settings && selectedMap.data.settings.SupportedBiomes)
				{
					if (Array.isArray(selectedMap.data.settings.SupportedBiomes))
						biomes.push(...selectedMap.data.settings.SupportedBiomes);
					else
					{
						const pathToBiomes = this.BiomesPath + selectedMap.data.settings.SupportedBiomes;
						const biomeFolder = Engine.ListDirectoryFiles(pathToBiomes, this.BiomesExtension, false);
						for (const biomeFile of biomeFolder)
							biomes.push(biomeFile.replace(this.BiomesPath, "").replace(this.BiomesExtension.replace("*", ""), ""));
					}
				}
			}
			this.rmBiome.list = biomes;
			this.rmBiome.selected = biomes.length > 0 ? 0 : -1;

			const playerPlacements = [];
			if (this.rmScript.selected >= 0 && this.randomMaps.length > 0)
			{
				const selectedMap = this.randomMaps[this.rmScript.selected];
				if (selectedMap && selectedMap.data && selectedMap.data.settings && selectedMap.data.settings.PlayerPlacements)
				{
					if (Array.isArray(selectedMap.data.settings.PlayerPlacements))
						playerPlacements.push(...selectedMap.data.settings.PlayerPlacements);
				}
			}
			this.rmPlacements.list = playerPlacements;
			this.rmPlacements.selected = playerPlacements.length > 0 ? 0 : -1;
		};
		this.rmScript.list = this.randomMaps.map(m => translate(m.data.settings.Name));
		this.rmScript.selected = this.randomMaps.length > 0 ? 0 : -1;

		this.mapSizes = Engine.ReadJSONFile(this.MapSizesPath);
		this.rmSize.list = this.mapSizes.Data.map(s => translate(s.Name));
		this.rmSize.selected = this.mapSizes.Data.length > 0 ? 0 : -1;

		this.defaulPlayerSettings = Engine.ReadJSONFile(this.DefaultPlayerSettings).PlayerData;
		// Remove gaia if present
		if (this.defaulPlayerSettings.length > 0 && this.defaulPlayerSettings[0].Name === "Gaia")
			this.defaulPlayerSettings.shift();
		this.rmPlayers.list = Array.from(this.defaulPlayerSettings, (_, i) => i + 1);
		this.rmPlayers.selected = this.defaulPlayerSettings.length - 1;
	}

	onGenerate()
	{
		const selectedScript = this.randomMaps[this.rmScript.selected];
		const mapSize = this.mapSizes.Data[this.rmSize.selected];
		const maxPlayers = this.rmPlayers.selected + 1;
		const biome = this.rmBiome.selected >= 0 ? this.rmBiome.list[this.rmBiome.selected] : null;
		const playerPlacement = this.rmPlacements.selected >= 0 ? this.rmPlacements.list[this.rmPlacements.selected] : null;
		this.session.transition({
			"mapType": "random",
			"script": selectedScript.file,
			"settings": {
				"PlayerData": this.defaulPlayerSettings.slice(0, maxPlayers),
				...selectedScript.data.settings,
				"Biome": biome,
				"PlayerPlacement": playerPlacement,
				"Seed": this.seed,
				"Size": mapSize.Tiles,
				"Nomad": false,
				"VictoryConditions": [],
				"AISeed": 0,
			},
		}, Provenances.GENERATED);
	}


}

RandomMapPage.prototype.GenerateCaption = translate("Generate");
RandomMapPage.prototype.RandomMapPath = "maps/random/";
RandomMapPage.prototype.DefaultPlayerSettings = "simulation/data/settings/player_defaults.json";
RandomMapPage.prototype.MapSizesPath = "simulation/data/settings/map_sizes.json";
RandomMapPage.prototype.RandomMapExtension = "*.json";
RandomMapPage.prototype.SizeCaption = translate("Size");
RandomMapPage.prototype.RerollCaption = translate("Reroll");
RandomMapPage.prototype.BiomesPath = "maps/random/rmbiome/";
RandomMapPage.prototype.BiomesExtension = "*.json";
