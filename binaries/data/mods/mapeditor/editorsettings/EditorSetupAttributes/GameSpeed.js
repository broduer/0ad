EditorSetupSettings.prototype.Attributes.GameSpeed = class extends EditorSetupSetting
{
	init()
	{
		this.speed = 0;
	}

	toInitAttributes(attribs)
	{
		attribs.gameSpeed = this.speed;
	}

	fromInitAttributes(attribs)
	{
		if (attribs.speed)
			this.speed = this.setSpeed(attribs.speed);
	}

	setSpeed(newSpeed)
	{
		this.speed = newSpeed;
	}
};
