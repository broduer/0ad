Trigger.prototype.InitCaptureTheRelic = function()
{
	const cmpTemplateManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager);
	const catafalqueTemplates = shuffleArray(cmpTemplateManager.FindAllTemplates(false).filter(
		name => GetIdentityClasses(cmpTemplateManager.GetTemplate(name).Identity || {}).indexOf("Relic") != -1));

	const potentialSpawnPoints = TriggerHelper.GetLandSpawnPoints();
	if (!potentialSpawnPoints.length)
	{
		error("No gaia entities found on this map that could be used as spawn points!");
		return;
	}

	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	const numSpawnedRelics = cmpEndGameManager.GetGameSettings().relicCount;
	this.totalRemainingRelics = numSpawnedRelics;
	this.playerRelicsCount = new Array(TriggerHelper.GetNumberOfPlayers()).fill(0, 1);
	this.playerRelicsCount[0] = numSpawnedRelics;

	for (let i = 0; i < numSpawnedRelics; ++i)
	{
		this.relics[i] = TriggerHelper.SpawnUnits(pickRandom(potentialSpawnPoints), catafalqueTemplates[i], 1, 0)[0];

		const cmpPositionRelic = Engine.QueryInterface(this.relics[i], IID_Position);
		cmpPositionRelic.SetYRotation(randomAngle());
	}
};

Trigger.prototype.CheckCaptureTheRelicVictory = function(data)
{
	const cmpIdentity = Engine.QueryInterface(data.entity, IID_Identity);
	if (!cmpIdentity || !cmpIdentity.HasClass("Relic") || data.from == INVALID_PLAYER)
		return;

	--this.playerRelicsCount[data.from];

	if (data.to == -1)
	{
		warn("Relic entity " + data.entity + " has been destroyed.");
		this.relics.splice(this.relics.indexOf(data.entity), 1);
		--this.totalRemainingRelics;
	}
	else
		++this.playerRelicsCount[data.to];

	const capturer = data.to == -1 ? undefined : data.to;
	this.DeleteCaptureTheRelicVictoryMessages();
	this.CheckCaptureTheRelicCountdown({ "capturer": capturer });

	// A started countdown has already sent its notifications.
	if (capturer !== undefined && !this.relicsVictoryTimer)
		this.SendRelicCaptureNotification(capturer);
};

Trigger.prototype.SendRelicCaptureNotification = function(playerID)
{
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	const total = this.totalRemainingRelics;
	const duration = this.CAPTURE_NOTIFICATION_DURATION;

	if (!this.relicsVictoryTimer)
	{
		const alliance = cmpEndGameManager.GetAlliedVictory() ?
			QueryPlayerIDInterface(playerID, IID_Diplomacy).GetMutualAllies().filter(pid => QueryPlayerIDInterface(pid).IsActive()) :
			[playerID];
		const parameters = {
			"count": alliance.reduce((sum, pid) => sum + this.playerRelicsCount[pid], 0),
			"total": total,
			"_player_": playerID
		};

		if (alliance.length == 1)
		{
			TriggerHelper.SendDualNotification(playerID,
				markForTranslation("You have captured a relic. You now have %(count)s of %(total)s relics."),
				markForTranslation("%(_player_)s has captured a relic. They now have %(count)s of %(total)s relics."),
				duration, parameters);
			return;
		}

		const nonAllies = [-1];
		for (let i = 1; i < TriggerHelper.GetNumberOfPlayers(); ++i)
		{
			const cmpPlayer = QueryPlayerIDInterface(i);
			if (alliance.indexOf(i) == -1 && cmpPlayer && cmpPlayer.GetState() != "defeated")
				nonAllies.push(i);
		}

		TriggerHelper.SendNotification(markForTranslation("You have captured a relic. Your alliance now has %(count)s of %(total)s relics."),
			[playerID], duration, parameters, ["_player_"]);
		TriggerHelper.SendNotification(markForTranslation("%(_player_)s has captured a relic. Your alliance now has %(count)s of %(total)s relics."),
			alliance.filter(pid => pid != playerID), duration, parameters, ["_player_"]);
		TriggerHelper.SendNotification(markForTranslation("%(_player_)s has captured a relic. Their alliance now has %(count)s of %(total)s relics."),
			nonAllies, duration, parameters, ["_player_"]);
		return;
	}

	const winningPlayers = this.relicsVictoryCountdownPlayers;
	const others = [-1];
	for (let i = 1; i < TriggerHelper.GetNumberOfPlayers(); ++i)
		if (winningPlayers.indexOf(i) == -1)
			others.push(i);

	const isTeam = winningPlayers.length > 1;
	const relicDuration = cmpEndGameManager.GetGameSettings().relicDuration;
	if (relicDuration)
	{
		const cmpGuiInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
		this.othersRelicsVictoryMessage = cmpGuiInterface.AddTimeNotification({
			"message": isTeam ?
				markForTranslation("%(_player_)s and their allies have captured all relics and will win in %(time)s.") :
				markForTranslation("%(_player_)s has captured all relics and will win in %(time)s."),
			"players": others,
			"parameters": {
				"_player_": playerID
			},
			"translateMessage": true,
			"translateParameters": ["_player_"]
		}, relicDuration);

		this.ownRelicsVictoryMessage = cmpGuiInterface.AddTimeNotification({
			"message": isTeam ?
				markForTranslation("You and your allies have captured all relics and will win in %(time)s.") :
				markForTranslation("You have captured all relics and will win in %(time)s."),
			"players": winningPlayers,
			"translateMessage": true
		}, relicDuration);
		return;
	}

	const victoryParameters = { "total": total, "_player_": playerID };
	if (!isTeam)
	{
		TriggerHelper.SendDualNotification(playerID,
			markForTranslation("You have captured all relics!"),
			markForTranslation("%(_player_)s has captured all %(total)s relics!"),
			duration, victoryParameters);
		return;
	}

	TriggerHelper.SendNotification(markForTranslation("You and your allies have captured all relics!"),
		[playerID], duration, victoryParameters, ["_player_"]);
	TriggerHelper.SendNotification(markForTranslation("%(_player_)s has captured the last relic! Your alliance has captured all relics!"),
		winningPlayers.filter(pid => pid != playerID), duration, victoryParameters, ["_player_"]);
	TriggerHelper.SendNotification(markForTranslation("%(_player_)s and their allies have captured all %(total)s relics!"),
		others, duration, victoryParameters, ["_player_"]);
};

/**
 * Check if a group of mutually allied players have acquired all relics.
 * The winning players are the relic owners and all players mutually allied to all relic owners.
 * Reset the countdown if the group of winning players changes or extends.
 * @param {Object} [data] - Holds the "capturer" when called after a relic capture.
 */
Trigger.prototype.CheckCaptureTheRelicCountdown = function(data)
{
	if (this.playerRelicsCount[0])
	{
		this.DeleteCaptureTheRelicVictoryMessages();
		return;
	}

	const activePlayers = Engine.QueryInterface(SYSTEM_ENTITY, IID_PlayerManager).GetActivePlayers();
	const relicOwners = activePlayers.filter(playerID => this.playerRelicsCount[playerID]);
	if (!relicOwners.length)
	{
		this.DeleteCaptureTheRelicVictoryMessages();
		return;
	}

	const winningPlayers = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager).GetAlliedVictory() ?
		activePlayers.filter(playerID => relicOwners.every(owner => QueryPlayerIDInterface(playerID, IID_Diplomacy).IsMutualAlly(owner))) :
		[relicOwners[0]];

	// All relicOwners should be mutually allied
	if (relicOwners.some(owner => winningPlayers.indexOf(owner) == -1))
	{
		this.DeleteCaptureTheRelicVictoryMessages();
		return;
	}

	// Reset the timer when playerAndAllies isn't the same as this.relicsVictoryCountdownPlayers
	if (winningPlayers.length != this.relicsVictoryCountdownPlayers.length ||
	    winningPlayers.some(player => this.relicsVictoryCountdownPlayers.indexOf(player) == -1))
	{
		this.relicsVictoryCountdownPlayers = winningPlayers;
		this.StartCaptureTheRelicCountdown(winningPlayers, data?.capturer);
	}
};

Trigger.prototype.DeleteCaptureTheRelicVictoryMessages = function()
{
	if (!this.relicsVictoryTimer)
		return;

	Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer).CancelTimer(this.relicsVictoryTimer);
	this.relicsVictoryTimer = undefined;

	const cmpGuiInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
	cmpGuiInterface.DeleteTimeNotification(this.ownRelicsVictoryMessage);
	cmpGuiInterface.DeleteTimeNotification(this.othersRelicsVictoryMessage);
	this.relicsVictoryCountdownPlayers = [];
};

Trigger.prototype.StartCaptureTheRelicCountdown = function(winningPlayers, capturer)
{
	const cmpTimer = Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer);

	if (this.relicsVictoryTimer)
	{
		cmpTimer.CancelTimer(this.relicsVictoryTimer);
		this.relicsVictoryTimer = undefined;

		const cmpGuiInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
		cmpGuiInterface.DeleteTimeNotification(this.ownRelicsVictoryMessage);
		cmpGuiInterface.DeleteTimeNotification(this.othersRelicsVictoryMessage);
	}

	if (!this.relics.length)
		return;

	for (let playerID = 1; playerID < TriggerHelper.GetNumberOfPlayers(); ++playerID)
		if (QueryPlayerIDInterface(playerID).HasWon())
			return;

	// The first relic owner represents the winning players when no capture triggered the countdown (diplomacy changed).
	const cmpPlayer = QueryOwnerInterface(this.relics[0], IID_Player);
	if (!cmpPlayer)
	{
		warn("Relic entity " + this.relics[0] + " has no owner.");
		this.relics.splice(0, 1);
		this.CheckCaptureTheRelicCountdown({ "capturer": capturer });
		return;
	}

	const captureTheRelicDuration = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager).GetGameSettings().relicDuration;
	this.relicsVictoryTimer = cmpTimer.SetTimeout(SYSTEM_ENTITY, IID_Trigger,
		"CaptureTheRelicVictorySetWinner", captureTheRelicDuration, winningPlayers);

	this.SendRelicCaptureNotification(capturer ?? cmpPlayer.GetPlayerID());
};

Trigger.prototype.CaptureTheRelicVictorySetWinner = function(winningPlayers)
{
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	cmpEndGameManager.MarkPlayersAsWon(
		winningPlayers,
		n => markForPluralTranslation(
			"%(lastPlayer)s has won (Capture the Relic).",
			"%(players)s and %(lastPlayer)s have won (Capture the Relic).",
			n),
		n => markForPluralTranslation(
			"%(lastPlayer)s has been defeated (Capture the Relic).",
			"%(players)s and %(lastPlayer)s have been defeated (Capture the Relic).",
			n));
};

{
	const cmpTrigger = Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger);
	cmpTrigger.relics = [];
	cmpTrigger.playerRelicsCount = [];
	cmpTrigger.totalRemainingRelics = 0;
	cmpTrigger.relicsVictoryTimer = undefined;
	cmpTrigger.ownRelicsVictoryMessage = undefined;
	cmpTrigger.othersRelicsVictoryMessage = undefined;
	cmpTrigger.relicsVictoryCountdownPlayers = [];

	cmpTrigger.DoAfterDelay(0, "InitCaptureTheRelic", {});
	cmpTrigger.RegisterTrigger("OnDiplomacyChanged", "CheckCaptureTheRelicCountdown", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnOwnershipChanged", "CheckCaptureTheRelicVictory", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnPlayerWon", "DeleteCaptureTheRelicVictoryMessages", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnPlayerDefeated", "CheckCaptureTheRelicCountdown", { "enabled": true });
}

Trigger.prototype.CAPTURE_NOTIFICATION_DURATION = 10000;
