toolsAtlasStates.ObjectPreview = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("objectPreview");
		this.workingMemory = workingMemory;
		this.commandManager = commandManager;
		this.guiFn = guiFn;
		this.changeStateFn = changeStateFn;

		this.rotationHotKeys = [
			SDLConstans.GUI_MAPPINGS_HOTKEYS.MAPEDITOR_OBJECTPREVIEW_ROTATEW,
			SDLConstans.GUI_MAPPINGS_HOTKEYS.MAPEDITOR_OBJECTPREVIEW_ROTATE
		];

		this.speedModifiersHotkeys = [
			SDLConstans.GUI_MAPPINGS_HOTKEYS.MAPEDITOR_OBJECTPREVIEW_SPEED_0,
			SDLConstans.GUI_MAPPINGS_HOTKEYS.MAPEDITOR_OBJECTPREVIEW_SPEED_1,
			SDLConstans.GUI_MAPPINGS_HOTKEYS.MAPEDITOR_OBJECTPREVIEW_SPEED_2
		];

		watcherFn("objectSettings", ["option", "angle", "playerId"], (option, angle, playerId) =>
		{
			this.workingMemory.entityOption = option;
			this.workingMemory.entityAngle = angle;
			this.workingMemory.playerId = playerId;
		});
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

	onTick(dt)
	{
		if (this.workingMemory.directionAngle !== 0)
		{
			this.workingMemory.entityAngle += this.workingMemory.directionAngle * dt * (Math.PI / 2.0 * this.workingMemory.speedModifier);
			this.callInterface();
		}
	}

	onHandleInputBeforeGui(ev, hoverObject)
	{
		if (hoverObject)
			return false;

		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey === "cancel")
		{
			this.workingMemory.position = { "x": 0, "y": 0 };
			this.changeStateFn(this.CancelState);
			return true;
		}
		return false;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.workingMemory.position = { "x": ev.x, "y": ev.y };
			this.callInterface();
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey.startsWith("camera"))
		{
			this.callInterface();
			return false;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDLConstans.SDL_MOUSE_BUTTONS.SDL_BUTTON_LEFT)
		{
			this.changeStateFn("objectPreviewCreate");
			return true;
		}
		else if (this.handleRotationAngle(ev))
			return true;
		return false;
	}

	handleRotationAngle(ev)
	{
		this.workingMemory.speedModifier = this.DefaultSpeedModifier;
		for (const hotkeyModifierIndex in this.speedModifiersHotkeys)
			if (Engine.HotkeyIsPressed(this.speedModifiersHotkeys[hotkeyModifierIndex]))
			{
				this.workingMemory.speedModifier = this.SpeedModifiers[hotkeyModifierIndex] || this.DefaultSpeedModifier;
				break;
			}

		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYDOWN && this.rotationHotKeys.includes(ev.hotkey))
		{
			this.workingMemory.directionAngle = this.rotationHotKeys.indexOf(ev.hotkey) === 0 ? 1 : -1;
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYUP && this.rotationHotKeys.includes(ev.hotkey))
		{
			this.workingMemory.directionAngle = 0;
			return true;
		}

		return false;
	}

	randomizeActorSeed()
	{
		this.workingMemory.actorSeed = Math.floor(2 + Math.random() * (16 - 2));
	}

	onEnter()
	{
		if (this.workingMemory.entityOption === "")
			throw new Error("No entity option selected");

		if (this.workingMemory.actorSeed === undefined)
			this.randomizeActorSeed();

		this.guiFn("objectSettings", true);
		this.workingMemory.directionAngle = 0;
		this.workingMemory.speedModifier = this.DefaultSpeedModifier;
		this.callInterface();
	}

	onLeave()
	{
		this.callInterface(true);
		this.guiFn("objectSettings", false);
	}
};

toolsAtlasStates.ObjectPreview.prototype.CancelState = "selector";
toolsAtlasStates.ObjectPreview.prototype.DefaultSpeedModifier = 1.0;
toolsAtlasStates.ObjectPreview.prototype.SpeedModifiers = [
	1.0 / 64.0,
	1.0 / 4.0,
	4.0
];
