EditorSetupSettings.prototype.Attributes.EditorData = class extends EditorSetupSetting
{
	init()
	{
		this.type = undefined;
	}

	toInitAttributes(attribs)
	{
		attribs.editorType = this.type;
	}

	fromInitAttributes(attribs)
	{
		if (attribs.editorType)
			this.setType(attribs.editorType);
	}

	setType(type)
	{
		this.type = type;
	}

	getTitleDescription()
	{
		const findResult = this.EditorOptions.filter(options => options.Type === this.type);
		return {
			"label": translateWithContext("editor setup", "Editor Type"),
			"value": findResult.length > 0 ? findResult[0].Name : translateWithContext("editor setup", "Undefined")
		};
	}
};

EditorSetupSettings.prototype.Attributes.EditorData.prototype.EditorOptions =
	[
		{
			"Name": translate("New Map"),
			"Tooltip": translate("you can start from scratch your map"),
			"Type": "new",
			"Default": true
		},
		{
			"Name": translate("Load Map"),
			"Tooltip": translate("you can load a map for editing"),
			"Type": "load"
		}
	];
