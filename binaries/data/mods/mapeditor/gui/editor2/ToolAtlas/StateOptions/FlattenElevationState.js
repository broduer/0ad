import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";
import { GUI_MAPPINGS_EVENTS, SDL_MOUSE_BUTTONS } from "gui/global/SDLConstants.js";
import { CommandNames } from "gui/editor2/Constants/CommandsController.js";

export class FlattenElevationState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super(StateNames.FLATTEN_ELEVATION);
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.changeStateFn = changeStateFn;
		this.guiFn = guiFn;
		this.enabled = false;

		watcherFn(ControllerNames.BRUSH_SETTINGS_CONTROLLER, ["info"], (info) =>
		{
			if (this.enabled && this.workingMemory[WorkingMemory.BRUSH])
				this.workingMemory[WorkingMemory.BRUSH].SetData(info.size, info.size, info.data);
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
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey.startsWith("camera"))
		{
			const position = this.workingMemory[WorkingMemory.POSITION];
			this.workingMemory[WorkingMemory.BRUSH].UpdatePosition(position.x, position.y);
			return false;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDL_MOUSE_BUTTONS.SDL_BUTTON_LEFT)
		{
			this.changeStateFn(StateNames.ELEVATION_TOOL);
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDL_MOUSE_BUTTONS.SDL_BUTTON_RIGHT)
		{
			this.changeStateFn(StateNames.ELEVATION_TOOL);
			return true;
		}

		return false;
	}

	onEnter()
	{
		if (!this.workingMemory[WorkingMemory.BRUSH])
			this.workingMemory[WorkingMemory.BRUSH] = MapEditor.CreateBrush();

		this.workingMemory[WorkingMemory.BRUSH].SetData(this.workingMemory[WorkingMemory.BRUSH_SIZE], this.workingMemory[WorkingMemory.BRUSH_SIZE], this.workingMemory[WorkingMemory.BRUSH_DATA]);
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(true);
		this.workingMemory[WorkingMemory.COMMAND_NAME] = CommandNames.FLATTEN_ELEVATION;
		this.workingMemory[WorkingMemory.BACK_TO_STATE] = this.name;
		this.workingMemory[WorkingMemory.PRIORITY_BRUSH] = 1;

		this.guiFn(GuiUISettings.BRUSH_SETTINGS, true);
		this.enabled = true;
	}

	onLeave()
	{
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(false);
		this.guiFn(GuiUISettings.BRUSH_SETTINGS, false);
		this.enabled = false;
	}
}
