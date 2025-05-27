EditorSetupSettings.prototype.Attributes.Map = class extends EditorSetupSetting
{
	init()
	{
		this.map = undefined;
	}

	toInitAttributes(attribs)
	{
		attribs.map = this.map;
	}

	fromInitAttributes(attribs)
	{
		if (attribs.map)
			this.setMap(attribs.map);
	}

	setMap(newMap)
	{
		this.map = newMap;
	}

	getTitleDescription()
	{
		return {
			"label": translateWithContext("editor setup", "Map"),
			"value": this.map
		};
	}
};
