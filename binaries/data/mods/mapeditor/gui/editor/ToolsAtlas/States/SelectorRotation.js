toolsAtlasStates.SelectorRotation = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("selectorRotation");
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
			this.commandManager.markCommandAsFinalized();
			this.cmdProc.undo();
			this.changeStateFn("selector");
			return true;
		}
		return false;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONUP)
		{
			this.changeStateFn("selector");
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			const fromGlobalAndIndividualCenterPoints = !Engine.HotkeyIsPressed("mapeditor.selector.rotation.gicp") && !Engine.HotkeyIsPressed("mapeditor.selector.rotation.gcp");
			const fromCenterPoint = Engine.HotkeyIsPressed("mapeditor.selector.rotation.gcp") || fromGlobalAndIndividualCenterPoints;

			if (fromCenterPoint != this.workingMemory.fromCenterPoint)
			{
				this.commandManager.markCommandAsFinalized();
				this.workingMemory.fromCenterPoint = fromCenterPoint;
			}

			this.workingMemory.position = { "x": ev.x, "y": ev.y };
			const position = Engine.GetTerrainAtScreenPoint(ev.x, ev.y);

			if (fromCenterPoint)
				this.commandManager.pushCommand("RotateObjectsFromCenterPoint", { "entities": this.workingMemory.selection, "position": position, "rotate": fromGlobalAndIndividualCenterPoints });
			else
				this.commandManager.pushCommand("RotateObject", { "entities": this.workingMemory.selection, "position": position });
			return true;
		}
		return false;
	}

	onEnter()
	{
		this.guiFn("objectSettings", true);

		if (this.workingMemory.selection.length === 0)
			throw new Error("No objects selected");

		this.workingMemory.fromCenterPoint = true;
	}

	onLeave()
	{
		this.guiFn("objectSettings", false);
		this.commandManager.markCommandAsFinalized();
	}
};
