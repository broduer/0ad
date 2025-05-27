import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";
import { GUI_MAPPINGS_EVENTS, GUI_MAPPINGS_HOTKEYS, SDL_MOUSE_BUTTONS } from "gui/global/SDLConstants.js";

export class PaintTerrainEyeDropperState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watchFn, setterFn)
	{
		super(StateNames.PAINT_TERRAIN_EYEDROPPER);
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.changeStateFn = changeStateFn;
		this.guiFn = guiFn;
		this.setterFn = setterFn;
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
			const texture = MapEditor.GetTerrainTexture(this.workingMemory[WorkingMemory.BRUSH]);
			this.setterFn(ControllerNames.GUI_CONTROLLER, "currentTerrainTexture", texture);
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_HOTKEYUP && ev.hotkey === GUI_MAPPINGS_HOTKEYS.MAPEDITOR_PAINT_EYEDROPPER)
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

		this.guiFn(GuiUISettings.PAINT_SETTINGS, true);
	}

	onLeave()
	{
		this.workingMemory[WorkingMemory.BRUSH].SetRenderEnabled(false);

		this.guiFn(GuiUISettings.PAINT_SETTINGS, false);
	}
}
