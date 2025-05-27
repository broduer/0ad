toolsAtlasStates.SelectorPickSimilar = class extends FSMv2State
{
	constructor(workingMemory, commandManager, changeStateFn, guiFn, watcherFn)
	{
		super("selectorPickSimilar");
		this.workingMemory = workingMemory;
		this.guiFn = guiFn;
		this.changeStateFn = changeStateFn;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === SDLConstans.GUI_MAPPINGS_EVENTS.SDL_MOUSEBUTTONUP)
		{
			const selectionAdd = Engine.HotkeyIsPressed("selection.add");
			const selectionRemove = Engine.HotkeyIsPressed("selection.remove");
			this.workingMemory.deselectEntities = [];

			const entities = MapEditor.PickSimilarEntities(this.workingMemory.selectorLastSelection, Engine.HotkeyIsPressed("mapeditor.selector.actors"));

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

			this.workingMemory.selectorLastSelection = 0;
			this.changeStateFn("selector");
			return true;
		}
		return false;
	}

	onEnter()
	{
		this.guiFn("objectSettings", true);

		if (!this.workingMemory.selectorLastSelection)
			throw new Error("No object selected");

	}

	onLeave()
	{
		this.guiFn("objectSettings", false);
	}
};
