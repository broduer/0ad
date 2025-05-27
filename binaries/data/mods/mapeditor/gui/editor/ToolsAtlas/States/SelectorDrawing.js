toolsAtlasStates.SelectorDrawing = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("selectorDrawing");
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

			this.workingMemory.position = { "x": ev.x, "y": ev.y };
			const position = Engine.GetTerrainAtScreenPoint(ev.x, ev.y);
			this.commandManager.pushCommand("MoveEntities", { "entities": this.workingMemory.selection, "position": position, "pivot": this.workingMemory.selectorLastSelection });
			return true;
		}
		return false;
	}

	onEnter()
	{
		this.guiFn("objectSettings", true);

		if (this.workingMemory.selectorLastSelection === 0)
			throw new Error("No object selected");
	}

	onLeave()
	{
		this.guiFn("objectSettings", false);
		this.commandManager.markCommandAsFinalized();
	}
};
