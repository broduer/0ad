GameSettings.prototype.Attributes.GameName = class GameName extends GameSetting
{
	init()
	{
		this.value = "";
	}

	toInitAttributes(attribs)
	{
		if (this.value)
			attribs.gameName = this.value;
	}

	fromInitAttributes(attribs)
	{
		if (attribs.gameName !== undefined)
			this.value = attribs.gameName;
	}
};
