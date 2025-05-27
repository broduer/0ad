import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { GUI_MAPPINGS_EVENTS } from "gui/global/SDLConstants.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";

export class ElevationToolState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn)
	{
		super(StateNames.ELEVATION_TOOL);
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.changeStateFn = changeStateFn;
		this.guiFn = guiFn;
	}

	onTick(dt)
	{
		this.commandManager.pushCommand(this.workingMemory[WorkingMemory.COMMAND_NAME], {
			"position": this.workingMemory[WorkingMemory.POSITION],
			"brush": this.workingMemory[WorkingMemory.BRUSH],
			"priority": this.workingMemory[WorkingMemory.PRIORITY_BRUSH] * this.workingMemory[WorkingMemory.STRENGTH] * dt,
		});
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.workingMemory[WorkingMemory.POSITION] = { "x": ev.x, "y": ev.y };
			this.workingMemory[WorkingMemory.BRUSH].UpdatePosition(ev.x, ev.y);
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONUP)
		{
			this.changeStateFn(this.workingMemory[WorkingMemory.BACK_TO_STATE]);
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

		if (!this.workingMemory[WorkingMemory.COMMAND_NAME])
			throw new Error("Command name not found");

		if (!this.workingMemory[WorkingMemory.STRENGTH])
			throw new Error("Strength not found");

		if (!this.workingMemory[WorkingMemory.BACK_TO_STATE])
			throw new Error("Back state not found");

		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(true);
		this.guiFn(GuiUISettings.BRUSH_SETTINGS, true);
	}

	onLeave()
	{
		this.commandManager.markCommandAsFinalized();
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(false);
		this.guiFn(GuiUISettings.BRUSH_SETTINGS, false);
	}
}
