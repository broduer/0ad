function LoadPlayerSettings(settings, newPlayers)
{
	const playerDefaults = Engine.ReadJSONFile("simulation/data/settings/player_defaults.json").PlayerData;

	const playerData = settings.PlayerData;
	if (!playerData)
		warn("Player.js: Setup has no player data - using defaults.");

	const getPlayerSetting = (idx, property) =>
	{
		if (playerData && playerData[idx] && (property in playerData[idx]))
			return playerData[idx][property];

		if (playerDefaults && playerDefaults[idx] && (property in playerDefaults[idx]))
			return playerDefaults[idx][property];

		return undefined;
	};

	// Add gaia to simplify iteration
	// (if gaia is not already the first civ such as when called from Atlas' ActorViewer)
	if (playerData && playerData[0] && (!playerData[0].Civ || playerData[0].Civ != "gaia"))
		playerData.unshift(null);

	const cmpPlayerManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_PlayerManager);
	let numPlayers = cmpPlayerManager.GetNumPlayers();

	if (newPlayers)
	{
		const settingsNumPlayers = playerData?.length ?? playerDefaults.length;

		while (numPlayers < settingsNumPlayers)
			cmpPlayerManager.AddPlayer(GetPlayerTemplateName(getPlayerSetting(numPlayers++, "Civ")));

		for (; numPlayers > settingsNumPlayers; numPlayers--)
			cmpPlayerManager.RemoveLastPlayer();
	}


	for (let i = 0; i < numPlayers; ++i)
	{
		const entityId = cmpPlayerManager.GetPlayerByID(i);
		const cmpPlayer = Engine.QueryInterface(entityId, IID_Player);
		const cmpIdentity = Engine.QueryInterface(entityId, IID_Identity);

		cmpIdentity.SetName(getPlayerSetting(i, "Name"));
		const color = getPlayerSetting(i, "Color");
		cmpPlayer.SetColor(color.r, color.g, color.b);

		// Special case for gaia
		if (i == 0)
			continue;

		const startCam = getPlayerSetting(i, "StartingCamera");
		if (startCam)
			cmpPlayer.SetStartCam(startCam.Position, startCam.Rotation);
	}
}

function GetPlayerTemplateName(civ)
{
	return "special/players/"+ civ;
}

Engine.RegisterGlobal("LoadPlayerSettings", LoadPlayerSettings);
