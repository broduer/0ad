import { WorkingMemory, StateNames } from "gui/editor2/Constants/ToolsAtlasController.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { FSMv2 } from "gui/global/FiniteStateMachine.js";
import { stateOptions } from "gui/editor2/ToolAtlas/StateOptions/index.js";
import { transitionOptions } from "gui/editor2/ToolAtlas/TransitionOptions/index.js";

export class ToolsAtlasController
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.workingMemory = {
			[WorkingMemory.SELECTION]: [],
			[WorkingMemory.TERRAIN_NAME]: "",
			[WorkingMemory.POSITION]: { "x": 0, "y": 0 },
		};

		this.states = Object.keys(stateOptions).sort(
			(a, b) => (stateOptions[a].ORDER || -1) - (stateOptions[b].ORDER || -1)
		);

		this.setupWindow.registerLoadHandler(() =>
		{
			this.commandMannager = this.setupWindow.controllers[ControllerNames.COMMANDS_CONTROLLER];
			this.guiController = this.setupWindow.controllers[ControllerNames.GUI_CONTROLLER];
			this.workingMemory[WorkingMemory.TERRAIN_NAME] = this.guiController.currentTerrainTexture;
			this.guiController.watch(()=>
			{
				this.workingMemory[WorkingMemory.TERRAIN_NAME] = this.guiController.currentTerrainTexture;
			}, ["currentTerrainTexture"]);

			const changeStateFn = (nextState) => this.changeState(nextState);
			const guiFn = (gui, visible) =>
			{
				const guiIsOpen = `${gui}IsOpen`;
				if (this.guiController[guiIsOpen] !== undefined)
					this.guiController[guiIsOpen] = !!visible;
				else
					warn(`${gui}IsOpen is not defined in guiController`);
			};
			const watcherFn = (ctrl, settings, fnWatch) =>
			{
				const handlerCtrl = this.setupWindow.controllers[ctrl];
				if (handlerCtrl === undefined)
				{
					warn(`${settings} is not defined in ${ctrl}`);
					return;
				}

				if (!Array.isArray(settings))
					settings = [settings];

				const fnAccesor = () => settings.reduce((acc, s) =>
				{
					acc.push(handlerCtrl[s]);
					return acc;
				}, []);

				handlerCtrl.watch(()=>
				{
					fnWatch(...fnAccesor());
				}, settings);

				fnWatch(...fnAccesor());
			};
			const setterFn = (ctrl, setting, value) =>
			{
				const handlerCtrl = this.setupWindow.controllers[ctrl];
				if (handlerCtrl === undefined)
				{
					warn(`${ctrl} is not defined`);
					return;
				}

				if (handlerCtrl[setting] === undefined)
				{
					warn(`${setting} is not defined in ${ctrl}`);
					return;
				}

				handlerCtrl[setting] = value;
			};

			const instanceStates = this.states.map(state =>
			{
				return new stateOptions[state](this.workingMemory, this.commandMannager, changeStateFn, guiFn, watcherFn, setterFn);
			});

			const defaultState = instanceStates.find(state => state.name === this.DefaultStateName);
			if (!defaultState)
				throw new Error(`${this.DefaultStateName} state not found`);

			this.fsm = new FSMv2(defaultState, transitionOptions);

			for (const state of instanceStates.filter(st => st.name !== this.DefaultStateName))
				this.fsm.addState(state);
		});

		this.setupWindow.registerClosePageHandler(() =>
		{
			this.changeState(this.DefaultStateName);
		});

		this.setupWindow.registerGetHotloadDataHandler((data) =>
		{
			this.changeState(this.DefaultStateName);
		});

		this.setupWindow.registerHandleTickHandler((dt) =>
		{
			if (this.fsm.currentState.onTick)
				this.fsm.currentState.onTick(dt);
		});

		this.setupWindow.registerGameLoadHandler(() =>
		{
			this.changeState(this.DefaultStateName);
		});

		this.setupWindow.registerGameUnloadHandler(() =>
		{
			this.changeState(this.DefaultStateName);
		});

		this.setupWindow.registerHandleInputAfterGuiHandler(this.onHandleInputAfterGui.bind(this));
		this.setupWindow.registerHandleInputBeforeGuiHandler(this.onHandleInputBeforeGui.bind(this));
	}

	onHandleInputAfterGui(ev)
	{
		if (this.fsm.currentState.onHandleInputAfterGui)
			return this.fsm.currentState.onHandleInputAfterGui(ev);
		return false;
	}

	onHandleInputBeforeGui(ev, hoveredObject)
	{
		if (this.fsm.currentState.onHandleInputBeforeGui)
			return this.fsm.currentState.onHandleInputBeforeGui(ev, hoveredObject);
		return false;
	}

	changeState(newState)
	{
		if (this.fsm.currentState.name === newState)
			return true;

		if (!this.fsm.canTransition(newState) && this.fsm.currentState.name !== this.DefaultStateName)
			this.fsm.transitionTo(this.DefaultStateName);

		const ok = this.fsm.transitionTo(newState);
		this.guiController.currentState = this.fsm.currentState.name;
		return ok;
	}
}

ToolsAtlasController.ORDER = 100; // last one
ToolsAtlasController.prototype.DefaultStateName = StateNames.IDLE;
