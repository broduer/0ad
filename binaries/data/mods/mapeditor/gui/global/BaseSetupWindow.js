export class BaseSetupWindow
{
	defaultBackPage = "page_pregame.xml";
	windowName = "setupWindow";

	constructor(initData, hotloadData, closePageCallback, ClassControllers, ClassSetupWindowPages)
	{
		this.loadHandlers = new Set();
		this.closePageHandlers = new Set();
		this.getHotloadDataHandlers = new Set();
		this.handleInputBeforeGuiHandlers = new Set();
		this.handleInputAfterGuiHandlers = new Set();
		this.handleTickHandlers = new Set();
		this.gameLoadHandlers = new Set();
		this.gameUnloadHandlers = new Set();

		if (!closePageCallback)
			throw new Error("closePageCallback is required.");

		this.closePageCallback = closePageCallback;

		this.backPage = undefined;
		if (initData?.backPage)
			this.backPage = initData.backPage;

		this.controllers = {};

		// controls will be sorting base on constant ORDER
		// if this Constant isnt define it set to -1
		const classControllers = Object.keys(ClassControllers || {}).sort(
			(a, b) => (ClassControllers[a].ORDER || -1) - (ClassControllers[b].ORDER || -1)
		);

		for (const classController of classControllers)
		{
			const name = classController.charAt(0).toLowerCase() + classController.slice(1);
			if (name in this.controllers)
				throw new Error("controller '" + name + "' is already used.");
			this.controllers[name] = new ClassControllers[classController](this);
		}

		// These are the pages within the setup window that may use the controls defined above
		/** @type {Object<string, any>} */
		this.pages = {};

		const classSetupWindowPages = Object.keys(ClassSetupWindowPages || {}).sort(
			(a, b) => (ClassSetupWindowPages[a].ORDER || -1) - (ClassSetupWindowPages[b].ORDER || -1)
		);
		for (const classSetupWindowPage of classSetupWindowPages)
		{
			const name = classSetupWindowPage.charAt(0).toUpperCase() + classSetupWindowPage.slice(1);
			if (name in this.pages)
				throw new Error("page '" + name + "' is already used.");
			this.pages[name] = new ClassSetupWindowPages[classSetupWindowPage](this);
		}

		this.window = Engine.GetGUIObjectByName(this.windowName);
		this.window.onTick = this.onHandleTick.bind(this);

		Object.freeze(this.controllers);
		Object.freeze(this.pages);

		// This event is triggered after all classes have been instantiated and subscribed to each others events
		for (const handler of this.loadHandlers)
			handler(initData, hotloadData);
	}

	registerGameLoadHandler(handler)
	{
		this.gameLoadHandlers.add(handler);
	}

	unregisterGameLoadHandler(handler)
	{
		this.gameLoadHandlers.delete(handler);
	}

	registerGameUnloadHandler(handler)
	{
		this.gameUnloadHandlers.add(handler);
	}

	unregisterGameUnloadHandler(handler)
	{
		this.gameUnloadHandlers.delete(handler);
	}

	registerLoadHandler(handler)
	{
		this.loadHandlers.add(handler);
	}

	unregisterLoadHandler(handler)
	{
		this.loadHandlers.delete(handler);
	}

	registerClosePageHandler(handler)
	{
		this.closePageHandlers.add(handler);
	}

	unregisterClosePageHandler(handler)
	{
		this.closePageHandlers.delete(handler);
	}

	registerGetHotloadDataHandler(handler)
	{
		this.getHotloadDataHandlers.add(handler);
	}

	unregisterGetHotloadDataHandler(handler)
	{
		this.getHotloadDataHandlers.delete(handler);
	}

	registerHandleInputBeforeGuiHandler(handler)
	{
		this.handleInputBeforeGuiHandlers.add(handler);
	}

	unregisterHandleInputBeforeGuiHandler(handler)
	{
		this.handleInputBeforeGuiHandlers.delete(handler);
	}

	registerHandleInputAfterGuiHandler(handler)
	{
		this.handleInputAfterGuiHandlers.add(handler);
	}

	unregisterHandleInputAfterGuiHandler(handler)
	{
		this.handleInputAfterGuiHandlers.delete(handler);
	}

	registerHandleTickHandler(handler)
	{
		this.handleTickHandlers.add(handler);
	}

	unregisterHandleTickHandler(handler)
	{
		this.handleTickHandlers.delete(handler);
	}

	getHotloadData()
	{
		const object = {};
		for (const handler of this.getHotloadDataHandlers)
			handler(object);
		return object;
	}

	closePage()
	{
		for (const handler of this.closePageHandlers)
			handler();

		if (this.backPage)
		{
			this.closePageCallback({ [Engine.openRequest]: {
				"page": this.backPage.page,
				"arguments": this.backPage?.data
			} });
			return;
		}

		this.closePageCallback({ [Engine.openRequest]: {
			"page": this.defaultBackPage
		} });
	}

	onHandleInputBeforeGui(ev, hoveredObject)
	{
		for (const handler of this.handleInputBeforeGuiHandlers)
			if (handler(ev, hoveredObject) === true)
				return true;

		return false;
	}

	onHandleInputAfterGui(ev)
	{
		for (const handler of this.handleInputAfterGuiHandlers)
			if (handler(ev) === true)
				return true;

		return false;
	}

	onHandleTick(dt)
	{
		for (const handler of this.handleTickHandlers)
			handler(dt);
	}

	onGameLoad(attribs)
	{
		for (const h of this.gameLoadHandlers)
			h(attribs);
	}

	onGameUnload()
	{
		for (const h of this.gameUnloadHandlers)
			h();
	}
}
