import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames, WorkingMemory } from "gui/editor2/Constants/ToolsAtlasController.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";

// this state is a stackgolder for working memoery used in PaintTerrain and ElevationTerrain
export class BrushSettingsState extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super(StateNames.BRUSH_SETTINGS);
		this.workingMemory = workingMemory;

		watcherFn(ControllerNames.BRUSH_SETTINGS_CONTROLLER, ["info", "strength"], (info, strength) =>
		{
			this.workingMemory[WorkingMemory.BRUSH_SIZE] = info.size;
			this.workingMemory[WorkingMemory.BRUSH_DATA] = info.data;
			this.workingMemory[WorkingMemory.STRENGTH] = this.STRENGTH_MULTIPLIER * (strength / this.STRENGTH_DIVIDER);
		});
	}
}

BrushSettingsState.prototype.STRENGTH_MULTIPLIER = 1024.0;
BrushSettingsState.prototype.STRENGTH_DIVIDER = 10.0;
