import { CommandNames } from "gui/editor2/Constants/CommandsController.js";
import { Command } from "gui/global/CommandPattern.js";

export class ReplaceTerrainCommand extends Command
{
	constructor(args)
	{
		super();
		this.position = args.position;
		this.textureName = args.terrainName;
		this.brush = args.brush;
		this.terrainTilesDelta = MapEditor.CreateTerrainTileArray();
		this.finalize = true; // not mergeable

		this.terrainTilesDelta.Init();
	}

	getType()
	{
		return CommandNames.REPLACE_TERRAIN;
	}

	execute()
	{
		this.brush.UpdatePosition(this.position.x, this.position.y);
		[this.i0, this.j0, this.i1, this.j1]= this.terrainTilesDelta.ReplaceTiles(this.textureName, this.brush);

		if (this.i0 === undefined)
			return;
		MapEditor.MakeDirtyTiles(this.i0, this.j0, this.i1, this.j1);
	}

	undo()
	{
		if (this.i0 === undefined)
			return;
		this.terrainTilesDelta.Undo();
		MapEditor.MakeDirtyTiles(this.i0, this.j0, this.i1, this.j1);
	}

	redo()
	{
		if (this.i0 === undefined)
			return;
		this.terrainTilesDelta.Redo();
		MapEditor.MakeDirtyTiles(this.i0, this.j0, this.i1, this.j1);
	}
}
