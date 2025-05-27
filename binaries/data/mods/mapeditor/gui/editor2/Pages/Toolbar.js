import { toolbarOptions } from "gui/editor2/Pages/ToolbarOptions/index.js";
import { layoutHorizontal } from "gui/global/Layouts.js";

export class ToolbarsPage
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;

		this.topToolbarButtonsPanel = Engine.GetGUIObjectByName("topToolbarButtonsPanel");
		const topToolbarButtons = this.topToolbarButtonsPanel.children;
		this.margin = topToolbarButtons[0].size.top;
		this.buttonWidth = topToolbarButtons[0].size.right;

		// Tooolbar buttons will be sorting base on constant ORDER
		// if this Constant isnt define it set to -1
		const handlerTopToolbarButtons = Object.keys(toolbarOptions).sort(
			(a, b) => (toolbarOptions[a].ORDER || -1) - (toolbarOptions[b].ORDER || -1)
		);

		if (handlerTopToolbarButtons.length > topToolbarButtons.length)
			throw new Error(
				"There are " + handlerTopToolbarButtons.length + " top toolbar buttons defined, " +
				"but only " + topToolbarButtons.length + " objects!"
			);

		this.defaultIndex = -1;
		this.buttons = handlerTopToolbarButtons.map((handlerName, i) =>
		{
			this.toogleIndex = toolbarOptions[handlerName].DEFAULT === true ? i : this.defaultIndex;
			this.defaultIndex = toolbarOptions[handlerName].DEFAULT === true ? i : this.defaultIndex;
			const icon = Engine.GetGUIObjectByName(`topToolbarButtonIcon[${i}]`);
			const selection = Engine.GetGUIObjectByName(`topToolbarButtonSelection[${i}]`);
			const handler = new toolbarOptions[handlerName](this, topToolbarButtons[i], icon, i);
			this.initButton(handler, topToolbarButtons[i], i);
			return {
				handler,
				icon,
				selection,
				"button": topToolbarButtons[i]
			};
		});

		layoutHorizontal(this.topToolbarButtonsPanel, this.margin);
		this.toogleIndex = this.defaultIndex;

		this.setupWindow.registerGameLoadHandler(()=>
		{
			// set by default
			if (this.defaultIndex >= 0)
				this.buttons[this.defaultIndex].button.onPress();
		});
	}

	initButton(handler, button, i)
	{
		button.onPress = () =>
		{
			if (i !== this.toogleIndex)
			{
				if (this.buttons[this.toogleIndex])
				{
					this.buttons[this.toogleIndex].handler.onToogle(false);
					this.buttons[this.toogleIndex].selection.hidden = true;
				}
				handler.onToogle(true);
			}
			this.buttons[i].selection.hidden = false;
			this.toogleIndex = i;
		};

		// const size = button.size;
		// size.left = this.buttonWidth * (i) + this.margin;
		// size.right = this.buttonWidth * (i + 1);
		// button.size = size;

		button.hidden = false;
	}

	updateToogleWithoutEvent(index, isToogle)
	{
		if (this.buttons[index])
			this.buttons[index].selection.hidden = !isToogle;

		if (isToogle)
			this.toogleIndex = index;
	}
}
