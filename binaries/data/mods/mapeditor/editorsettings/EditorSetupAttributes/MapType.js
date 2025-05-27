EditorSetupSettings.prototype.Attributes.MapType = class extends EditorSetupSetting
{
	init(owner)
	{
		this.mapType = undefined;
		this.MapTypes = owner.Helpers.MapTypes;
	}

	toInitAttributes(attribs)
	{
		attribs.mapType = this.mapType;
	}

	fromInitAttributes(attribs)
	{
		if (attribs.mapType)
			this.mapType = this.setMapType(attribs.mapType);
	}

	setMapType(newMapType)
	{
		this.mapType = newMapType;
	}

	getTitleDescription()
	{
		const findResult = this.MapTypes.filter(options => options.Name === this.mapType);
		return {
			"label": translateWithContext("editor setup", "Map Type"),
			"value": findResult.length > 0 ? findResult[0].Title : translateWithContext("editor setup", "Undefined")
		};
	}
};
