GameSettings.prototype.Attributes.HostName = class HostName extends GameSetting
{
	init()
	{
		this.value = "";
	}

	toInitAttributes(attribs)
	{
		if (this.value)
			attribs.hostName = this.value;
	}

	fromInitAttributes(attribs)
	{
		if (attribs.hostName !== undefined)
			this.value = attribs.hostName;
	}
};
