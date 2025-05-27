import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { GUI_MAPPINGS_EVENTS } from "gui/global/SDLConstants.js";
import { CommandNames } from "gui/editor2/Constants/CommandsController.js";

export class PaintTerrainWithPriorityState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn)
	{
		super(StateNames.PAINT_TERRAIN_WITH_PRIORITY);
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.changeStateFn = changeStateFn;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.workingMemory[WorkingMemory.POSITION] = { "x": ev.x, "y": ev.y };
			this.workingMemory[WorkingMemory.BRUSH].UpdatePosition(ev.x, ev.y);

			this.commandManager.pushCommand(CommandNames.PAINT_TERRAIN, {
				"position": this.workingMemory[WorkingMemory.POSITION],
				"brush": this.workingMemory[WorkingMemory.BRUSH],
				"priority": this.workingMemory[WorkingMemory.PRIORITY_BRUSH],
				"terrainName": this.workingMemory[WorkingMemory.TERRAIN_NAME],
			});

			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONUP)
		{
			this.changeStateFn(StateNames.PAINT_TERRAIN);
			return true;
		}
		return false;
	}

	onEnter()
	{
		if (!this.workingMemory[WorkingMemory.BRUSH])
			throw new Error("Brush not found");

		if (!this.workingMemory[WorkingMemory.PRIORITY_BRUSH])
			throw new Error("Priority brush not found");

		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(true);

		// send the command directly
		this.commandManager.pushCommand(CommandNames.PAINT_TERRAIN, {
			"position": this.workingMemory[WorkingMemory.POSITION],
			"brush": this.workingMemory[WorkingMemory.BRUSH],
			"priority": this.workingMemory[WorkingMemory.PRIORITY_BRUSH],
			"terrainName": this.workingMemory[WorkingMemory.TERRAIN_NAME],
		});
	}

	onLeave()
	{
		this.commandManager.markCommandAsFinalized();
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(false);
	}
}
