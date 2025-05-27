import { ControllerNames } from "gui/editor2/Constants/Controllers.js";
import { GuiUISettings } from "gui/editor2/Constants/GuiController.js";
import { shapeOptions } from "gui/editor2/Pages/ShapeOptions/index.js";
import { brushOptions } from "gui/editor2/Pages/BrushOptions/index.js";
import { InfoSettings } from "gui/editor2/Constants/BrushSettingsController.js";

export class BrushSettingsPage
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.brushSettingsGui = Engine.GetGUIObjectByName("brushSettings");
		this.brushTool = Engine.GetGUIObjectByName("brushTool");
		this.brushShape = Engine.GetGUIObjectByName("brushShape");

		this.brushSize = Engine.GetGUIObjectByName("brushSize");
		this.brushSize.min_value = 0.01;
		this.brushSize.max_value = 1;

		this.brushStrength = Engine.GetGUIObjectByName("brushStrength");
		this.brushStrength.min_value = 0.01;
		this.brushStrength.max_value = 1;

		this.selfUpdating = false;

		this.setupWindow.registerLoadHandler(()=>
		{
			this.guiController = this.setupWindow.controllers[ControllerNames.GUI_CONTROLLER];
			this.brushSettings = this.setupWindow.controllers[ControllerNames.BRUSH_SETTINGS_CONTROLLER];
			this.toolsAtlasController = this.setupWindow.controllers[ControllerNames.TOOLS_ATLAS_CONTROLLER];

			this.guiController.watch(()=>
			{
				this.brushSettingsGui.hidden = !this.guiController[GuiUISettings.BRUSH_SETTINGS + "IsOpen"];
			}, [GuiUISettings.BRUSH_SETTINGS + "IsOpen"]);

			this.brushSettings.watch(()=>
			{
				this.brushSize.tooltip = `${this.brushSettings.info[InfoSettings.SIZE]}`;
			}, ["info"]);

			this.brushSettings.watch(()=>
			{
				this.brushTool.selected = this.brushTool.list_data.indexOf(this.brushSettings.tool);
			}, ["tool"]);

			this.brushSettings.watch(()=>
			{
				if (!this.selfUpdating)
					this.brushStrength.value = this.brushSettings.strength / 100.0;

				this.brushStrength.tooltip = `${this.brushSettings.strength}`;
			}, ["strength"]);

			this.brushSize.onValueChange = () =>
			{
				this.selfUpdating = true;
				const newSize = parseInt(this.brushSize.value * 100);
				this.brushSettings.info = {
					[InfoSettings.SIZE]: newSize,
					[InfoSettings.DATA]: shapeOptions[this.brushShape.list_data[this.brushShape.selected]].onChange(newSize)
				};
				this.selfUpdating = false;
			};

			this.brushStrength.onValueChange = () =>
			{
				this.selfUpdating = true;
				this.brushSettings.strength = parseInt(this.brushStrength.value * 100);
				this.selfUpdating = false;
				this.brushStrength.tooltip = `${this.brushSettings.strength}`;
			};
			this.init();
		});
	}

	init()
	{
		// tools will be sorting base on constant ORDER
		// if this Constant isnt define it set to -10
		const toolsOptions = Object.keys(brushOptions).sort(
			(a, b) =>
			{
				if (brushOptions[a].order !== undefined && brushOptions[b].order === undefined)
					return -1;
				if (brushOptions[a].order === undefined && brushOptions[b].order !== undefined)
					return 1;
				return brushOptions[a].order - brushOptions[b].order;
			}
		);

		this.brushTool.list = toolsOptions.map((tool)=> brushOptions[tool].name);
		this.brushTool.list_data = toolsOptions;
		this.brushTool.onSelectionChange = () =>
		{
			const selected = brushOptions[this.brushTool.list_data[this.brushTool.selected]];

			if (!selected)
				return;

			this.brushTool.tooltip = selected.tooltip;
			this.brushSettings.tool = this.brushTool.list_data[this.brushTool.selected];

			// we have a state to set
			if (selected.state)
				this.toolsAtlasController.changeState(selected.state);
		};
		this.brushTool.onHoverChange = () =>
		{
			const selected = brushOptions[this.brushTool.list_data[this.brushTool.hovered]];

			if (!selected)
				return;

			this.brushTool.tooltip = selected.tooltip;
		};

		this.brushShape.list = Object.keys(shapeOptions).map((shape)=> shapeOptions[shape].name);
		this.brushShape.list_data = Object.keys(shapeOptions);
		this.brushShape.onSelectionChange = () =>
		{
			const selected = shapeOptions[this.brushShape.list_data[this.brushShape.selected]];

			this.brushSettings.info= {
				[InfoSettings.SIZE]: this.brushSettings.info[InfoSettings.SIZE],
				[InfoSettings.DATA]: selected.onChange(this.brushSettings.info[InfoSettings.SIZE])
			};

			this.brushShape.tooltip = selected.tooltip;
		};

		this.brushSettings.info = {
			[InfoSettings.SIZE]: 4,
			[InfoSettings.DATA]: []
		};
		this.brushSettings.strength = 10;

		this.brushShape.selected = 0;
	}
}
