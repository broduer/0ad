globalThis.PlayerID = -1;

export function BaseAI(settings)
{
	if (!settings)
		return;

	this.player = settings.player;
}

/** Return a simple object (using no classes etc) that will be serialized into saved games */
BaseAI.prototype.Serialize = function()
{
	return {};
};

/**
 * Called after the constructor when loading a saved game, with 'data' being
 * whatever Serialize() returned
 */
BaseAI.prototype.Deserialize = function(data, sharedScript)
{
};

BaseAI.prototype.Init = function(playerID, sharedAI)
{
	PlayerID = playerID;

	this.territoryMap = sharedAI.territoryMap;
	this.accessibility = sharedAI.accessibility;

	this.gameState = sharedAI.gameState[this.player];
	this.gameState.ai = this;

	this.timeElapsed = sharedAI.timeElapsed;

	this.CustomInit(this.gameState);
};

/** AIs override this function */
BaseAI.prototype.CustomInit = function()
{
};

BaseAI.prototype.HandleMessage = function(state, playerID, sharedAI)
{
	PlayerID = playerID;
	this.territoryMap = sharedAI.territoryMap;
	this.OnUpdate(sharedAI);
};

/** AIs override this function */
BaseAI.prototype.OnUpdate = function()
{
};

BaseAI.prototype.chat = function(message)
{
	Engine.PostCommand(PlayerID, { "type": "aichat", "message": message });
};
