toolsAtlasStates.ObjectPreviewCreate = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("objectPreviewCreate");
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.guiFn = guiFn;
		this.changeStateFn = changeStateFn;
	}

	callInterface(clean = false)
	{
		if (clean)
		{
			MapEditor.MapEditorInterfaceCall("SetObjectPreview", { "template": "" });
			return;
		}
		const position = Engine.GetTerrainAtScreenPoint(this.workingMemory.position.x, this.workingMemory.position.y);
		MapEditor.MapEditorInterfaceCall("SetObjectPreview", {
			"template": this.workingMemory.entityOption,
			"x": position.x,
			"z": position.z,
			"angle": this.workingMemory.entityAngle,
			"actorSeed": this.workingMemory.actorSeed,
			"player": this.workingMemory.playerId
		});
	}

	onHandleInputBeforeGui(ev, hoverObject)
	{
		if (hoverObject)
			return false;

		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey === "cancel")
		{
			this.changeStateFn(this.CancelState);
			return true;
		}
		return false;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			const position = Engine.GetTerrainAtScreenPoint(this.workingMemory.position.x, this.workingMemory.position.y);
			const mousePosition = Engine.GetTerrainAtScreenPoint(ev.x, ev.y);

			this.workingMemory.newPosition = { "x": ev.x, "y": ev.y };
			this.workingMemory.entityAngle = Math.atan2(mousePosition.x - position.x, mousePosition.z - position.z);
			this.callInterface();
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONUP && ev.button === SDLConstans.SDL_MOUSE_BUTTONS.SDL_BUTTON_LEFT)
		{
			const position = Engine.GetTerrainAtScreenPoint(this.workingMemory.position.x, this.workingMemory.position.y);
			this.commandManager.pushCommand("CreateObject", [{
				"template": this.workingMemory.entityOption,
				"x": position.x,
				"z": position.z,
				"angle": this.workingMemory.entityAngle,
				"actorSeed": this.workingMemory.actorSeed,
				"player": this.workingMemory.playerId
			}]);
			this.workingMemory.actorSeed = undefined;

			this.workingMemory.position = this.workingMemory.newPosition || this.workingMemory.position;
			this.changeStateFn("objectPreview");
			return true;
		}
		return false;
	}

	onEnter()
	{
		if (this.workingMemory.entityOption === "")
			throw new Error("No entity option selected");

		if (this.workingMemory.actorSeed === undefined)
			throw new Error("No actor seed selected");

		this.guiFn("objectSettings", true);
		this.callInterface();
	}

	onLeave()
	{
		this.callInterface(true);
		this.workingMemory.newPosition = undefined;
		this.guiFn("objectSettings", false);
	}
};

toolsAtlasStates.ObjectPreviewCreate.prototype.CancelState = "objectPreview";
