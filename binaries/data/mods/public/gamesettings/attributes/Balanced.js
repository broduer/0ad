GameSettings.prototype.Attributes.Balanced = class Balanced extends GameSetting
{
	init()
	{
		this.enabled = false;
		this.settings.map.watch(() => this.onMapChange(), ["map"]);
	}

	toInitAttributes(attribs)
	{
		if (this.settings.balanced)
		{
			attribs.settings.Balanced = this.enabled;
		}
	}

	fromInitAttributes(attribs)
	{
		this.setEnabled(!!this.getLegacySetting(attribs, "Balanced"));
	}

	setEnabled(enabled)
	{
		this.enabled = enabled;
	}

	onMapChange()
	{
		if (!this.getMapSetting("Balanced"))
		{
			this.enabled = false;
			return;
		}
	}
};
