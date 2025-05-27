import { TerrainGridBrowserPage } from "gui/editor2/Pages/TerrainGridBrowser.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";

export class PaintSettingsPage
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.paintSettingsGui = Engine.GetGUIObjectByName("paintSettings");
		this.texturePreview = Engine.GetGUIObjectByName("texturePreview");
		this.texturePreviewName = Engine.GetGUIObjectByName("texturePreviewName");
		this.terrainGroup = Engine.GetGUIObjectByName("terrainGroup");
		this.terrainBrowserContainer = new TerrainGridBrowserPage(Engine.GetGUIObjectByName("terrainBrowserContainer"));

		this.setupWindow.registerLoadHandler(()=>
		{
			this.guiController = this.setupWindow.controllers[ControllerNames.GUI_CONTROLLER];

			this.guiController.watch(()=>
			{
				this.paintSettingsGui.hidden = !this.guiController[GuiUISettings.PAINT_SETTINGS + "IsOpen"];
			}, [GuiUISettings.PAINT_SETTINGS + "IsOpen"]);

			this.guiController.watch(()=>
			{
				this.texturePreview.texture_name = this.guiController.currentTerrainTexture;
				this.texturePreviewName.caption = this.formatTextureName(this.guiController.currentTerrainTexture);
			}, ["currentTerrainTexture"]);


			this.texturePreview.texture_name = this.guiController.currentTerrainTexture;
			this.texturePreviewName.caption = this.formatTextureName(this.guiController.currentTerrainTexture);

			this.terrainBrowserContainer.registerSelectionChangeHandler(() =>
			{
				const texture = this.terrainBrowserContainer.terrainList[this.terrainBrowserContainer.selected] || "";
				if (texture)
					this.guiController.currentTerrainTexture = texture;
			});

			this.terrainGroup.onSelectionChange = () =>
			{
				const textureList = MapEditor.GetTerrainTextures(this.terrainGroup.list_data[this.terrainGroup.selected]);
				this.terrainBrowserContainer.updateTerrainList(textureList, this.texturePreview.texture_name);
			};
		});

		this.setupWindow.registerGameLoadHandler(()=>
		{
			const terrainGroup = MapEditor.GetTerrainGroups();
			this.terrainGroup.list_data = terrainGroup;
			this.terrainGroup.list = terrainGroup;

			this.terrainGroup.selected = 0;
		});
	}

	formatTextureName(name)
	{
		// replace underscores with spaces, and upper case the first letter
		return name.replace(/_/g, " ").replace(/^\w/, c => c.toUpperCase());
	}
}
