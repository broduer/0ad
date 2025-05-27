toolsAtlasStates.SelectorPasting = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("selectorPasting");
		this.workingMemory = workingMemory;
		this.guiFn = guiFn;
		this.changeStateFn = changeStateFn;
		this.commandManager = commandManager;
	}

	onHandleInputBeforeGui(ev, hoverObject)
	{
		if (hoverObject)
			return false;

		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey === "cancel")
		{
			this.changeStateFn("selector");
			return true;
		}
		return false;
	}

	callInterface(clean = false)
	{
		if (clean)
		{
			MapEditor.MapEditorInterfaceCall("SetObjectPreview", { "template": "" });
			return;
		}
		const position = Engine.GetTerrainAtScreenPoint(this.workingMemory.position.x, this.workingMemory.position.y);
		const direction = { "x": position.x - this.workingMemory.pastingCenter.x, "z": position.z - this.workingMemory.pastingCenter.z };
		for (const pastingObject of this.workingMemory.pasting)
		{
			MapEditor.MapEditorInterfaceCall("SetObjectPreview", {
				"template": pastingObject.template,
				"x": pastingObject.x + direction.x,
				"z": pastingObject.z + direction.z,
				"angle": pastingObject.angle,
				"actorSeed": pastingObject.seed,
				"player": this.workingMemory.playerId,
				"cleanObjectPreviews": false
			});
		}
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDLConstans.SDL_MOUSE_BUTTONS.SDL_BUTTON_LEFT)
		{
			const position = Engine.GetTerrainAtScreenPoint(this.workingMemory.position.x, this.workingMemory.position.y);
			const direction = { "x": position.x - this.workingMemory.pastingCenter.x, "z": position.z - this.workingMemory.pastingCenter.z };
			const cmdResult = this.commandManager.pushCommand("CreateObject", this.workingMemory.pasting.map(o => ({
				"template": o.template,
				"x": o.x + direction.x,
				"z": o.z + direction.z,
				"angle": o.angle,
				"seed": o.seed,
				"player": this.workingMemory.playerId
			})));

			// select the newly created objects
			this.workingMemory.selection = cmdResult.entitiesId;
			this.callInterface(true);
			this.changeStateFn("selector");
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.workingMemory.position = { "x": ev.x, "y": ev.y };
			this.callInterface(true);
			this.callInterface();
			return true;
		}
		return false;
	}

	onEnter()
	{
		if (!this.workingMemory.pasting || this.workingMemory.pasting.length === 0)
			throw new Error("Nothing to paste");

		if (this.workingMemory.playerId === undefined)
			throw new Error("No player selected");

		// calculate the center point of the pasting objects
		this.workingMemory.pastingCenter = { "x": 0, "z": 0 };
		const min = { "x": Infinity, "z": Infinity };
		const max = { "x": -Infinity, "z": -Infinity };
		for (const pastingObject of this.workingMemory.pasting)
		{
			min.x = Math.min(min.x, pastingObject.x);
			min.z = Math.min(min.z, pastingObject.z);
			max.x = Math.max(max.x, pastingObject.x);
			max.z = Math.max(max.z, pastingObject.z);
		}

		this.workingMemory.pastingCenter.x = (min.x + max.x) / 2;
		this.workingMemory.pastingCenter.z = (min.z + max.z) / 2;
		this.callInterface();
	}

	onLeave()
	{
		this.callInterface(true);
	}
};
