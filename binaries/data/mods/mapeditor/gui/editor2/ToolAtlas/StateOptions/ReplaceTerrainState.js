import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { GUI_MAPPINGS_EVENTS, SDL_MOUSE_BUTTONS } from "gui/global/SDLConstants.js";
import { CommandNames } from "gui/editor2/Constants/CommandsController.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";

export class ReplaceTerrainState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn)
	{
		super(StateNames.REPLACE_TERRAIN);
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.changeStateFn = changeStateFn;
		this.guiFn = guiFn;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.workingMemory[WorkingMemory.POSITION] = { "x": ev.x, "y": ev.y };
			this.workingMemory[WorkingMemory.BRUSH].UpdatePosition(ev.x, ev.y);
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDL_MOUSE_BUTTONS.SDL_BUTTON_LEFT)
		{
			this.commandManager.pushCommand(CommandNames.REPLACE_TERRAIN, {
				"position": this.workingMemory[WorkingMemory.POSITION],
				"brush": this.workingMemory[WorkingMemory.BRUSH],
				"terrainName": this.workingMemory[WorkingMemory.TERRAIN_NAME],
			});
			return true;
		}
		return false;
	}

	onEnter()
	{
		if (!this.workingMemory[WorkingMemory.BRUSH])
			throw new Error("Brush not found");

		const size = 2;
		const data = [];

		for (let y = 0; y < size; ++y)
		{
			for (let x = 0; x < size; ++x)
			{
				data.push(1.0);
			}
		}

		this.workingMemory[WorkingMemory.BRUSH].SetData(size, size, data);
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(true);

		this.guiFn(GuiUISettings.BRUSH_SETTINGS, true);
		this.guiFn(GuiUISettings.PAINT_SETTINGS, true);
	}

	onLeave()
	{
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(false);

		this.guiFn(GuiUISettings.BRUSH_SETTINGS, false);
		this.guiFn(GuiUISettings.PAINT_SETTINGS, false);
	}
}
