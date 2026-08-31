GameSettings.prototype.Attributes.RestrictNomadPlacement = class RestrictNomadPlacement extends GameSetting
{
	init()
	{
		this.enabled = false;
	}

	toInitAttributes(attribs)
	{
		if (this.settings.map.type == "random" && this.settings.nomad.enabled)
			attribs.settings.RestrictNomadPlacement = this.enabled;
	}

	fromInitAttributes(attribs)
	{
		this.setEnabled(!!this.getLegacySetting(attribs, "RestrictNomadPlacement"));
	}

	setEnabled(enabled)
	{
		this.enabled = enabled;
	}
};
