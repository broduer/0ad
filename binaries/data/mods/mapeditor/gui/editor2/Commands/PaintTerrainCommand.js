import { CommandNames } from "gui/editor2/Constants/CommandsController.js";
import { Command } from "gui/global/CommandPattern.js";

export class PaintTerrainCommand extends Command
{
	constructor(args)
	{
		super();
		this.position = args.position;
		this.textureName = args.terrainName;
		this.priority = args.priority;
		this.brush = args.brush;
		this.terrainTilesDelta = MapEditor.CreateTerrainTileArray();

		this.terrainTilesDelta.Init();
	}

	getType()
	{
		return CommandNames.PAINT_TERRAIN;
	}

	execute()
	{
		this.brush.UpdatePosition(this.position.x, this.position.y);
		const bottonLeft = this.brush.GetBottomLeft();
		const width = this.brush.GetWidth();
		const height = this.brush.GetHeight();

		this.terrainTilesDelta.PaintTiles(this.textureName, this.brush, this.priority);

		this.i0 = bottonLeft.x - 1;
		this.j0 = bottonLeft.y - 1;
		this.i1 = bottonLeft.x + width + 1;
		this.j1 = bottonLeft.y + height + 1;

		MapEditor.MakeDirtyTiles(this.i0, this.j0, this.i1, this.j1);
	}

	undo()
	{
		this.terrainTilesDelta.Undo();
		MapEditor.MakeDirtyTiles(this.i0, this.j0, this.i1, this.j1);
	}

	redo()
	{
		this.terrainTilesDelta.Redo();
		MapEditor.MakeDirtyTiles(this.i0, this.j0, this.i1, this.j1);
	}

	mergeWith(command)
	{
		if (this.getType() !== command.getType())
			return;

		command.terrainTilesDelta.OverlayWith(this.terrainTilesDelta);
		command.i0 = Math.min(command.i0, this.i0);
		command.j0 = Math.min(command.j0, this.j0);
		command.i1 = Math.max(command.i1, this.i1);
		command.j1 = Math.max(command.j1, this.j1);
	}
}
