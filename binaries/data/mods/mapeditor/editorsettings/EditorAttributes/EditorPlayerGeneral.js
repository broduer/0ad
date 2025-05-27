EditorSettings.prototype.Attributes.EditorPlayerGeneral = class extends EditorSetting
{
	init()
	{
		var defaultValues = this.FN_LOAD_DEFAULT();
		this.playerNumber = 2;
		this.playerData = [];
		this.playerIndex = 0;
		this.maxPlayer = defaultValues.MAX_PLAYERS;
		this.defaultCiv = defaultValues.DEFAULT_CIV;
		this.getPropertyDefault = defaultValues.GET_PROPERTY_DEFAULT;

		this.currentName = "";
		this.currentCiv = "";
		this.currentAI = "";
	}

	toInitAttributes(attribs)
	{
		attribs.settings.PlayerData = this.playerData.map(p =>
		{
			if (p.AI && p.AI === 'none')
				p.AI = undefined;
			return p;
		});
	}

	fromInitAttributes(attribs)
	{
		this.playerData = attribs.settings.PlayerData || [];

		// Skipt Gaia
		if (this.playerData && this.playerData[0] == null)
			this.playerData.shift();

		this.setPlayerNumber(this.playerData.length);
	}

	setPlayerData(playerData, index)
	{
		this.playerData[index] = playerData || { "civ": this.defaultCiv };
	}

	setPlayerNumber(playerNumber)
	{
		const newPlayerNumber = Math.max(0, Math.min(playerNumber, this.maxPlayer));
		let currenPlayerNumber = this.playerNumber;
		// add new player data
		while (currenPlayerNumber < newPlayerNumber)
			this.setPlayerData(undefined, currenPlayerNumber++);

		// remove player data
		while (this.playerData.length > newPlayerNumber)
			this.playerData.splice(-1);

		this.playerNumber = newPlayerNumber;
		this.setPlayerIndex(this.playerIndex);
	}

	setPlayerIndex(playerIndex)
	{
		this.playerIndex = Math.max(0, Math.min(playerIndex, this.playerNumber - 1));

		this.currentName = this.playerData[this.playerIndex].Name || this.getPropertyDefault(this.playerIndex + 1, "Name");
		this.currentCiv = this.playerData[this.playerIndex].Civ || this.getPropertyDefault(this.playerIndex + 1, "Civ");
		this.currentAI = this.playerData[this.playerIndex].AI || this.getPropertyDefault(this.playerIndex + 1, "AI");
	}

	setCurrentName(newName)
	{
		this.currentName = newName;
		this.playerData[this.playerIndex].Name = this.currentName;
	}

	setCurrentCiv(newCiv)
	{
		this.currentCiv = newCiv;
		this.playerData[this.playerIndex].Civ = this.currentCiv;
	}

	setCurrentAI(newAI)
	{
		this.currentAI = newAI;
		this.playerData[this.playerIndex].AI = this.currentAI;
	}
};

EditorSettings.prototype.Attributes.EditorPlayerGeneral.prototype.FN_LOAD_DEFAULT = () =>
{
	let playerDefaults = Engine.ReadJSONFile("simulation/data/settings/player_defaults.json").PlayerData;

	// remove gaia settings
	if (playerDefaults && playerDefaults[0] == null)
		playerDefaults.shift();
	if (!playerDefaults)
		playerDefaults = [];

	return {
		"MAX_PLAYERS": playerDefaults.length,
		"DEFAULT_CIV": "editor",
		"GET_PROPERTY_DEFAULT": (idx, property) =>
			(playerDefaults && playerDefaults[idx] && (property in playerDefaults[idx])) ? playerDefaults[idx][property] : undefined
	};
};
