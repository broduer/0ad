EditorWindow.prototype.ClassSetupWindowPages.ObjectSettings = class
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.objectSettingsGui = Engine.GetGUIObjectByName("objectSettings");
		this.objectTool = Engine.GetGUIObjectByName("objectTool");
		this.objectToolOptions = Engine.GetGUIObjectByName("objectToolOptions");
		this.filter = Engine.GetGUIObjectByName("objectFilter");
		this.exactMath = Engine.GetGUIObjectByName("objectExactMatch");

		this.filter.tooltip = this.FilterTooltip;
		this.exactMath.tooltip = this.ExactMatchTooltip;

		this.setupWindow.registerLoadHandler(()=>
		{
			this.guiController = this.setupWindow.controls.guiController;
			this.objectSettings = this.setupWindow.controls.objectSettings;
			this.toolsAtlasController = this.setupWindow.controls.toolsAtlasController;

			this.guiController.watch(()=>
			{
				this.objectSettingsGui.hidden = !this.guiController.objectSettingsIsOpen;
			}, ["objectSettingsIsOpen"]);

			this.init();
		});
	}

	filterObjectOptionTools(selected, filter)
	{
		// we have a state to set
		if (selected.onChange)
		{
			const objectToolOptions = selected.onChange(filter) || {};
			this.objectToolOptions.list = objectToolOptions.labels || [];
			this.objectToolOptions.list_data = objectToolOptions.values || [];

			this.objectToolOptions.selected = -1;
		}
	}

	init()
	{
		// tools will be sorting base on constant ORDER
		// if this Constant isnt define it set to -1
		const toolsOptions = Object.keys(this.Tools).sort(
			(a, b) => (this.Tools[a].order || -1) - (this.Tools[b].order || -1)
		);

		this.objectTool.list = toolsOptions.map((tool)=> this.Tools[tool].name);
		this.objectTool.list_data = toolsOptions;
		this.objectTool.onSelectionChange = () =>
		{
			const selected = this.Tools[this.objectTool.list_data[this.objectTool.selected]];

			if (!selected)
				return;

			this.objectTool.tooltip = selected.tooltip || "";
			this.objectSettings.tool = this.objectTool.list_data[this.objectTool.selected];

			this.filterObjectOptionTools(selected, this.filter.caption);
		};

		this.objectToolOptions.onSelectionChange = () =>
		{
			if (this.objectToolOptions.selected == -1)
				return;

			const selected = this.Tools[this.objectTool.list_data[this.objectTool.selected]];
			const option = this.objectToolOptions.list_data[this.objectToolOptions.selected];

			if (!selected)
				return;

			this.objectSettings.option = option;

			if (selected.state)
				this.toolsAtlasController.changeState(selected.state, true);
		};

		this.objectToolOptions.onHoverChange = () =>
		{
			const selected = this.objectToolOptions.list[this.objectToolOptions.hovered];

			if (!selected)
				return;

			this.objectToolOptions.tooltip = selected;
		};

		this.filter.onTextEdit = () =>
		{
			const selected = this.Tools[this.objectTool.list_data[this.objectTool.selected]];

			if (!selected)
				return;

			this.filterObjectOptionTools(selected, this.filter.caption);
		};

		// select the first objectTool by default?
		this.objectTool.selected = 0;
	}
};

EditorWindow.prototype.ClassSetupWindowPages.ObjectSettings.prototype.FilterTooltip = translateWithContext("Map Editor", "Enter text to filter object list");
EditorWindow.prototype.ClassSetupWindowPages.ObjectSettings.prototype.ExactMatchTooltip = translateWithContext("Map Editor", "Provides a search with a strict string equality");

Object.defineProperty(EditorWindow.prototype.ClassSetupWindowPages.ObjectSettings.prototype, "Tools", {
	"value": {},
	"enumerable": false,
	"writable": true,
});

const objectTools = EditorWindow.prototype.ClassSetupWindowPages.ObjectSettings.prototype.Tools;
