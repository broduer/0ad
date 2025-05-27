toolsAtlasStates.SelectorBandbox = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("selectorBandbox");
		this.workingMemory = workingMemory;
		this.guiFn = guiFn;
		this.changeStateFn = changeStateFn;
		this.bandBoxGUI = Engine.GetGUIObjectByName("bandbox");
	}

	updateBandbox(x1, y1, x2, y2, hidden = false)
	{
		const scale = +Engine.ConfigDB_GetValue("user", "gui.scale");
		const xMin = Math.min(x1, x2);
		const yMin = Math.min(y1, y2);
		const xMax = Math.max(x1, x2);
		const yMax = Math.max(y1, y2);

		this.bandBoxGUI.size = { "left": xMin / scale, "top": yMin / scale, "right": xMax / scale, "bottom": yMax / scale };
		this.workingMemory.rect = [xMin, yMin, xMax, yMax];
		this.bandBoxGUI.hidden = hidden;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONUP)
		{
			const entities = MapEditor.PickEntitiesInRect(...this.workingMemory.rect, Engine.HotkeyIsPressed("mapeditor.selector.actors"));
			const selectionAdd = Engine.HotkeyIsPressed("selection.add");
			const selectionRemove = Engine.HotkeyIsPressed("selection.remove");
			this.workingMemory.deselectEntities = [];

			if (!selectionAdd && !selectionRemove)
			{
				this.workingMemory.deselectEntities = [...this.workingMemory.selection.filter(e => !entities.includes(e))];
				this.workingMemory.selection = entities;
			}
			else
			{
				for (const ent of entities)
				{
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
						this.workingMemory.selection.push(ent);
					}
				}
			}

			this.workingMemory.highlightList = this.workingMemory.highlightList.filter(e => !this.workingMemory.selection.includes(e));
			this.workingMemory.selectorLastSelection = 0;
			this.changeStateFn("selector");
			return true;
		}
		else if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEMOTION)
		{
			this.updateBandbox(this.workingMemory.position.x, this.workingMemory.position.y, ev.x, ev.y);
			return true;
		}
		return false;
	}

	onEnter()
	{
		this.guiFn("objectSettings", true);
		this.updateBandbox(0, 0, 0, 0, true);
	}

	onLeave()
	{
		this.guiFn("objectSettings", false);
		this.updateBandbox(0, 0, 0, 0, true);
	}
};
