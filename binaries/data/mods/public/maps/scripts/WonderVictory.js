Trigger.prototype.WonderVictoryEntityRenamed = function(data)
{
	if (!Engine.QueryInterface(data.newentity, IID_Wonder))
		return;

	// Check if wonder victory is enabled
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	if (!cmpEndGameManager)
		return;

	const victoryConditions = cmpEndGameManager.GetVictoryConditions();
	if (!victoryConditions || !victoryConditions.includes("wonder"))
		return;

	// Handle timer transfer (only if old entity had the timer)
	if (this.wonderVictoryMessages[data.entity])
	{
		this.WonderVictoryDeleteTimer(data.newentity);
		this.wonderVictoryMessages[data.newentity] = this.wonderVictoryMessages[data.entity];
		delete this.wonderVictoryMessages[data.entity];
	}

	// Make the completed wonder permanently visible
	TriggerHelper.MakeEntityPermanentlyVisible(data.newentity);

	// Send "wonder completed" notifications
	const cmpOwnership = Engine.QueryInterface(data.newentity, IID_Ownership);
	if (cmpOwnership)
	{
		const owner = cmpOwnership.GetOwner();
		if (owner > 0)
		{
			TriggerHelper.SendWonderNotifications(
				owner,
				markForTranslation("%(_player_)s has built a Wonder!"),
				markForTranslation("You have built a Wonder!")
			);
		}
	}
};

Trigger.prototype.WonderVictoryOwnershipChanged = function(data)
{
	if (!Engine.QueryInterface(data.entity, IID_Wonder))
		return;

	// Check if wonder victory is enabled
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	if (!cmpEndGameManager)
		return;

	const victoryConditions = cmpEndGameManager.GetVictoryConditions();
	if (!victoryConditions || !victoryConditions.includes("wonder"))
		return;

	this.WonderVictoryDeleteTimer(data.entity);

	if (data.to > 0)
		this.WonderVictoryStartTimer(data.entity, data.to);
};

Trigger.prototype.WonderVictoryDiplomacyChanged = function(data)
{
	if (!Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager).GetAlliedVictory())
		return;

	for (const ent in this.wonderVictoryMessages)
	{
		if (this.wonderVictoryMessages[ent].playerID != data.player && this.wonderVictoryMessages[ent].playerID != data.otherPlayer)
			continue;

		const owner = this.wonderVictoryMessages[ent].playerID;
		const otherPlayer = owner == data.player ? data.otherPlayer : data.player;
		const newAllies = new Set(QueryPlayerIDInterface(owner, IID_Diplomacy).GetPlayersByDiplomacy("IsExclusiveMutualAlly"));
		if (newAllies.has(otherPlayer) && !this.wonderVictoryMessages[ent].allies.has(otherPlayer) ||
		    !newAllies.has(otherPlayer) && this.wonderVictoryMessages[ent].allies.has(otherPlayer))
		{
			this.WonderVictoryDeleteTimer(ent);
			this.WonderVictoryStartTimer(ent, owner);
		}
	}
};

/**
 * Create new messages, and start timer to register defeat.
 */
Trigger.prototype.WonderVictoryStartTimer = function(ent, player)
{
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	const allies = cmpEndGameManager.GetAlliedVictory() ?
		QueryPlayerIDInterface(player, IID_Diplomacy).GetPlayersByDiplomacy("IsExclusiveMutualAlly") : [];

	const others = [-1];
	for (let playerID = 1; playerID < TriggerHelper.GetNumberOfPlayers(); ++playerID)
	{
		const cmpPlayer = QueryPlayerIDInterface(playerID);
		if (cmpPlayer.GetState() == "won")
			return;
		if (allies.indexOf(playerID) == -1 && playerID != player)
			others.push(playerID);
	}

	const cmpGuiInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
	const cmpTimer = Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer);

	const wonderDuration = cmpEndGameManager.GetGameSettings().wonderDuration;
	this.wonderVictoryMessages[ent] = {
		"playerID": player,
		"allies": new Set(allies),
		"timer": cmpTimer.SetTimeout(SYSTEM_ENTITY, IID_Trigger, "WonderVictorySetWinner", wonderDuration, player),
		"messages": [
			cmpGuiInterface.AddTimeNotification(
				{
					"message": allies.length ?
						markForTranslation("%(_player_)s owns a Wonder and %(_player_)s and their allies will win in %(time)s.") :
						markForTranslation("%(_player_)s owns a Wonder and will win in %(time)s."),
					"players": others,
					"parameters": {
						"_player_": player
					},
					"translateMessage": true,
					"translateParameters": []
				},
				wonderDuration),
			cmpGuiInterface.AddTimeNotification(
				{
					"message": markForTranslation("%(_player_)s owns a Wonder and you will win in %(time)s."),
					"players": allies,
					"parameters": {
						"_player_": player
					},
					"translateMessage": true,
					"translateParameters": []
				},
				wonderDuration),
			cmpGuiInterface.AddTimeNotification(
				{
					"message": allies.length ?
						markForTranslation("You own a Wonder and you and your allies will win in %(time)s.") :
						markForTranslation("You own a Wonder and will win in %(time)s."),
					"players": [player],
					"translateMessage": true
				},
				wonderDuration)
		]
	};
};

Trigger.prototype.WonderVictoryDeleteTimer = function(ent)
{
	if (!this.wonderVictoryMessages[ent])
		return;

	const cmpGuiInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
	const cmpTimer = Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer);

	for (const message of this.wonderVictoryMessages[ent].messages)
		cmpGuiInterface.DeleteTimeNotification(message);
	cmpTimer.CancelTimer(this.wonderVictoryMessages[ent].timer);
	delete this.wonderVictoryMessages[ent];
};

Trigger.prototype.WonderVictoryPlayerWon = function(data)
{
	for (const ent in this.wonderVictoryMessages)
		this.WonderVictoryDeleteTimer(ent);
};

Trigger.prototype.WonderVictorySetWinner = function(playerID)
{
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	cmpEndGameManager.MarkPlayerAndAlliesAsWon(
		playerID,
		n => markForPluralTranslation(
			"%(lastPlayer)s has won (wonder victory).",
			"%(players)s and %(lastPlayer)s have won (wonder victory).",
			n),
		n => markForPluralTranslation(
			"%(lastPlayer)s has been defeated (wonder victory).",
			"%(players)s and %(lastPlayer)s have been defeated (wonder victory).",
			n));
};

Trigger.prototype.WonderStartNotification = function(data)
{
	// Check if wonder victory is enabled in game settings
	const cmpEndGameManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_EndGameManager);
	if (!cmpEndGameManager)
		return;

	// Don't show the wonder foundation if it's not a wonder victory game
	const victoryConditions = cmpEndGameManager.GetVictoryConditions();
	if (!victoryConditions || !victoryConditions.includes("wonder"))
		return;

	const cmpTemplateManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager);
	if (!cmpTemplateManager)
		return;

	const template = cmpTemplateManager.GetTemplate(data.template);

	if (!template || !("Wonder" in template))
		return;

	const owner = TriggerHelper.GetOwner(data.foundation);
	if (owner <= 0)
		return;

	TriggerHelper.MakeEntityPermanentlyVisible(data.foundation, 50);

	TriggerHelper.SendWonderNotifications(
		owner,
		markForTranslation("%(_player_)s has started building a Wonder."),
		markForTranslation("You have started building a Wonder.")
	);
};

{
	const cmpTrigger = Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger);
	cmpTrigger.RegisterTrigger("OnEntityRenamed", "WonderVictoryEntityRenamed", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnOwnershipChanged", "WonderVictoryOwnershipChanged", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnDiplomacyChanged", "WonderVictoryDiplomacyChanged", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnPlayerWon", "WonderVictoryPlayerWon", { "enabled": true });
	cmpTrigger.RegisterTrigger("OnConstructionStarted", "WonderStartNotification", { "enabled": true });
	cmpTrigger.wonderVictoryMessages = {};
}
