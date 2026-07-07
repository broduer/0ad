GameSettings.prototype.Attributes.StartTime = class extends GameSetting
{
	onFinalizeAttributes(attribs)
	{
		attribs.startTime = Date.now();
	}
};
