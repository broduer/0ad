export class TerrainGridBrowserItemPage extends GridBrowserItem
{
	constructor(terrainGridBrowser, imageObject, itemIndex)
	{
		super(terrainGridBrowser, imageObject, itemIndex);

		this.terrainGridBrowser = terrainGridBrowser;

		this.terraintName = Engine.GetGUIObjectByName("terrainName[" + itemIndex + "]");
		this.terraintPreview = Engine.GetGUIObjectByName("terrainPreview[" + itemIndex + "]");

		terrainGridBrowser.registerSelectionChangeHandler(this.onSelectionChange.bind(this));
		terrainGridBrowser.registerPageChangeHandler(this.onGridResize.bind(this));

		this.imageObject.onMouseLeftDoubleClick = this.onMouseLeftDoubleClick.bind(this);
	}

	onSelectionChange()
	{
		this.updateSprite();
	}

	onGridResize()
	{
		super.onGridResize();
		this.updateTerrainAssignment();
		this.updateSprite();
	}

	updateSprite()
	{
		this.imageObject.sprite =
			this.gridBrowser.selected == this.itemIndex + this.gridBrowser.currentPage * this.gridBrowser.itemsPerRow ?
				this.SelectedSprite :
				"";
	}

	updateTerrainAssignment()
	{
		const terrain = this.gridBrowser.terrainList[
			this.itemIndex + this.gridBrowser.currentPage * this.gridBrowser.itemsPerRow] || undefined;
		if (!terrain)
			return;

		this.terraintName.caption = terrain.replace(/_/g, " ").replace(/^\w/, c => c.toUpperCase());
		this.terraintPreview.texture_name = terrain;
	}


	onMouseLeftDoubleClick()
	{
		// do something here
	}
}

TerrainGridBrowserItemPage.prototype.SelectedSprite = "color: 237 227 167 255";
