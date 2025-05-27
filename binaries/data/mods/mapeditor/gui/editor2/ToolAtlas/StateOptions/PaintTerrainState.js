import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";
import { GUI_MAPPINGS_EVENTS, GUI_MAPPINGS_HOTKEYS, SDL_MOUSE_BUTTONS } from "gui/global/SDLConstants.js";
export class PaintTerrainState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super(StateNames.PAINT_TERRAIN);
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
			this.workingMemory[WorkingMemory.PRIORITY_BRUSH] = 1;
			this.changeStateFn(StateNames.PAINT_TERRAIN_WITH_PRIORITY);
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDL_MOUSE_BUTTONS.SDL_BUTTON_RIGHT)
		{
			this.workingMemory[WorkingMemory.PRIORITY_BRUSH] = -1;
			this.changeStateFn(StateNames.PAINT_TERRAIN_WITH_PRIORITY);
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey === GUI_MAPPINGS_HOTKEYS.MAPEDITOR_PAINT_EYEDROPPER)
		{
			this.changeStateFn(StateNames.PAINT_TERRAIN_EYEDROPPER);
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

		this.guiFn(GuiUISettings.BRUSH_SETTINGS, true);
		this.guiFn(GuiUISettings.PAINT_SETTINGS, true);
		this.enabled = true;
	}

	onLeave(nextState)
	{
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(false);
		if (!nextState.startsWith("paint"))
		{
			this.guiFn(GuiUISettings.BRUSH_SETTINGS, false);
			this.guiFn(GuiUISettings.PAINT_SETTINGS, false);
		}
		this.enabled = false;
	}
}
