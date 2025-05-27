import { FSMv2State } from "gui/global/FiniteStateMachine.js";
import { StateNames } from "gui/editor2/Constants/ToolsAtlasController.js";
export class IdleState extends FSMv2State
{
	constructor()
	{
		super(StateNames.IDLE);
	}
}
