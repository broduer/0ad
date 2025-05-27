import { TerrainGridBrowserItemPage } from "gui/editor2/Pages/TerrainGridBrowserItem.js";
export class TerrainGridBrowserPage extends GridBrowser
{
	constructor(guiObject)
	{
		super(guiObject);

		this.terrainList = [];

		this.items = this.container.children.map((imageObject, itemIndex) =>
			new TerrainGridBrowserItemPage(this, imageObject, itemIndex));

		this.container.onWindowResized = this.onWindowResized.bind(this);
	}

	updateTerrainList(terrainList, currentTerrainName)
	{
		this.terrainList = terrainList;
		this.itemCount = terrainList.length;

		this.resizeGrid();

		this.setSelectedIndex(this.terrainList.indexOf(currentTerrainName) || 0);
		this.goToPageOfSelected();
	}
}

TerrainGridBrowserPage.prototype.ItemRatio = 4 / 3;
TerrainGridBrowserPage.prototype.DefaultItemWidth = 200;
TerrainGridBrowserPage.prototype.MinItemWidth = 200;
