toolsAtlasStates.Selector = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("selector");
		this.workingMemory = workingMemory;
		this.guiFn = guiFn;
		this.changeStateFn = changeStateFn;
		this.commandManager = commandManager;

		this.workingMemory.highlightList = [];
		this.workingMemory.deselectEntities= [];
	}

	onCopy()
	{
		const info = MapEditor.MapEditorInterfaceCall("GetCmpInfo", { "entities": this.workingMemory.selection });
		const objects = info.data.map(o => ({
			"template": o.template,
			"x": o.position.position.x,
			"z": o.position.position.z,
			"angle": o.position.rotation.y,
			"seed": o.visual.seed
		}));
		MapEditor.SendToClipboard(JSON.stringify(objects));
	}

	onPaste()
	{
		const stringData = MapEditor.GetFromClipboard();
		try
		{
			const json = JSON.parse(stringData);
			if (!Array.isArray(json))
				warn("Clipboard data is not an array");

			if (this.workingMemory.selection.length > 0)
			{
				this.noticeSelectionChangeSim(this.workingMemory.selection, false);
				this.workingMemory.selection = [];
			}

			// validate that objects contain template, x, z, angle and seed
			for (const o of json)
			{
				if (typeof o.template !== "string" || typeof o.x !== "number" || typeof o.z !== "number" || typeof o.angle !== "number" || typeof o.seed !== "number")
				{
					warn("Invalid object data " + uneval(o));
					return;
				}
			}
			this.workingMemory.pasting = json;
			this.changeStateFn("selectorPasting");
		}
		catch(e)
		{
			warn("Error parsing clipboard data", e);
			return;
		}
	}
	onHandleInputBeforeGui(ev, hoverObject)
	{
		if (hoverObject)
			return false;

		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey === "cancel")
		{
			this.noticeSelectionChangeSim(this.workingMemory.selection, false);
			this.workingMemory.selection = [];
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey && ev.hotkey === "mapeditor.selector.delete")
		{
			if (this.workingMemory.selection.length > 0)
			{
				this.commandManager.pushCommand("DeleteEntities", { "entities": this.workingMemory.selection });
				this.workingMemory.selection = [];
			}
			return true;
		}
		return false;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.workingMemory.position = { "x": ev.x, "y": ev.y };
			const ent = MapEditor.PickEntityAtPoint(ev.x, ev.y, Engine.HotkeyIsPressed("mapeditor.selector.actors"));

			this.setHighlightList(ent != 0 && !this.workingMemory.selection.includes(ent) ? [ent] : []);
			return true;
		}
		else if (this.workingMemory.selection.length > 0 && ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDLConstans.SDL_MOUSE_BUTTONS.SDL_BUTTON_RIGHT)
		{
			this.changeStateFn("selectorRotation");
			return true;
		}
		else if (this.workingMemory.selection.length > 0 && ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey === "copy")
		{
			this.onCopy();
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey === "paste")
		{
			this.onPaste();
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONDOWN && ev.button === SDLConstans.SDL_MOUSE_BUTTONS.SDL_BUTTON_LEFT)
		{
			if (ev.clicks === 2 && this.workingMemory.selectorLastSelection !== 0)
			{
				this.changeStateFn("selectorPickSimilar");
				return true;
			}

			const ent = MapEditor.PickEntityAtPoint(ev.x, ev.y, Engine.HotkeyIsPressed("mapeditor.selector.actors"));
			if (ent != 0)
			{
				this.workingMemory.deselectEntities = [];
				const selectionAdd = Engine.HotkeyIsPressed("selection.add");
				const selectionRemove = Engine.HotkeyIsPressed("selection.remove");
				let changeState = "";
				const isSelected = this.workingMemory.selection.includes(ent);

				if (selectionRemove)
				{
					if (isSelected)
					{
						this.workingMemory.deselectEntities.push(ent);
						this.workingMemory.selection = this.workingMemory.selection.filter(e => e !== ent);
					}
				}
				else if (!isSelected)
				{
					if (!selectionAdd)
					{
						this.workingMemory.deselectEntities = [...this.workingMemory.selection];
						this.workingMemory.selection = [];
					}

					this.workingMemory.selection.push(ent);
				}

				this.workingMemory.selectorLastSelection = ent;
				if (!selectionAdd && !selectionRemove && this.workingMemory.selection.length > 0)
				{
					// TODO: calculate offset
					changeState = "selectorDrawing";
				}

				// remove entities from highlight list
				this.workingMemory.highlightList = this.workingMemory.highlightList.filter(e => !this.workingMemory.selection.includes(e));
				this.noticeSelectionChangeSim(this.workingMemory.deselectEntities, false);
				this.workingMemory.deselectEntities = [];

				this.noticeSelectionChangeSim(this.workingMemory.selection, true);

				if (changeState)
					this.changeStateFn(changeState);
			}
			else
			{
				// BandBoxing
				this.workingMemory.selectorLastSelection = 0;
				this.changeStateFn("selectorBandbox");
			}
			return true;
		}
		return false;
	}

	setHighlightList(list)
	{
		if (this.workingMemory.highlightList.length > 0)
			MapEditor.MapEditorInterfaceCall("HighlightSelectable", { "entities": this.workingMemory.highlightList, "alpha": 0, "selected": false });

		this.workingMemory.highlightList = list;

		if (this.workingMemory.highlightList.length > 0)
			MapEditor.MapEditorInterfaceCall("HighlightSelectable", { "entities": this.workingMemory.highlightList, "alpha": 0.75, "selected": true });
	}

	noticeSelectionChangeSim(entities, selected)
	{
		MapEditor.MapEditorInterfaceCall("HighlightSelectable", { "entities": entities, "alpha": selected ? 1.0 : 0.0, "selected": selected });
	}

	onEnter()
	{
		this.guiFn("objectSettings", true);

		if (this.workingMemory.highlightList.length > 0)
			this.setHighlightList([]);

		if (this.workingMemory.selection.length > 0)
			this.noticeSelectionChangeSim(this.workingMemory.selection, true);

		if (this.workingMemory.deselectEntities.length > 0)
			this.noticeSelectionChangeSim(this.workingMemory.deselectEntities, false);
	}

	onLeave(nextStateName)
	{
		this.setHighlightList([]);

		if (!nextStateName.startsWith("selector"))
		{
			this.guiFn("objectSettings", false);
			if (this.workingMemory.selection.length > 0)
			{
				this.noticeSelectionChangeSim(this.workingMemory.selection, false);
				this.workingMemory.selection = [];
			}
		}
	}
};
