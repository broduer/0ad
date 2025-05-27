EditorSettings.prototype.Attributes.EditorPublicGeneral = class extends EditorSetting
{
	init()
	{
		this.allyView = false;
		this.lockTeams = false;
	}

	toInitAttributes(attribs)
	{
		attribs.settings.AllyView = this.allyView;
		attribs.settings.LockTeams = this.lockTeams;
	}

	fromInitAttributes(attribs)
	{
		this.setAllyView(!!attribs.settings.AllyView);
		this.setLockTeams(!!attribs.settings.LockTeams);
	}

	setAllyView(allyView)
	{
		this.allyView = allyView;
	}

	setLockTeams(lockTeams)
	{
		this.lockTeams = lockTeams;
	}
};
