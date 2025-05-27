EditorWindow.prototype.ClassSetupWindowPages.LeftToolbarsPage = class
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;

		this.leftToolbarButtonsPanel = Engine.GetGUIObjectByName("leftToolbarButtonsPanel");
		const leftToolbarButtons = this.leftToolbarButtonsPanel.children;
		this.margin = leftToolbarButtons[0].size.top;
		this.buttonWidth = leftToolbarButtons[0].size.right;

		// Tooolbar buttons will be sorting base on constant ORDER
		// if this Constant isnt define it set to -1
		const handlerLeftToolbarButtons = Object.keys(this.Toolbars).sort(
			(a, b) => (this.Toolbars[a].ORDER || -1) - (this.Toolbars[b].ORDER || -1)
		);

		if (handlerLeftToolbarButtons.length > leftToolbarButtons.length)
			throw new Error(
				"There are " + handlerLeftToolbarButtons.length + " left toolbar buttons defined, " +
				"but only " + leftToolbarButtons.length + " objects!"
			);

		this.buttons = handlerLeftToolbarButtons.map((handlerName, i) =>
		{
			const icon = Engine.GetGUIObjectByName(`leftToolbarButtonIcon[${i}]`);
			const handler = new this.Toolbars[handlerName](this, leftToolbarButtons[i], icon);
			this.initButton(handler, leftToolbarButtons[i], i);
			return handler;
		});
	}

	initButton(handler, button, i)
	{
		button.onPress = () =>
		{
			handler.onPress();
		};

		const size = button.size;
		size.left = this.buttonWidth * (i) + this.margin;
		size.right = this.buttonWidth * (i + 1);
		button.size = size;

		button.hidden = false;
	}
};

const leftToolbarButtons = EditorWindow.prototype.ClassSetupWindowPages.LeftToolbarsPage.prototype;

Object.defineProperty(leftToolbarButtons, "Toolbars", {
	"value": {},
	"enumerable": false,
	"writable": true,
});
