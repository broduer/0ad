class PlayerManager
{
	Init()
	{
		this.playerEntities = [];
	}

	GetNumPlayers()
	{
		return this.playerEntities.length;
	}

	GetPlayerByID(id)
	{
		if (id in this.playerEntities)
			return this.playerEntities[id];

		if (id == INVALID_PLAYER)
			return INVALID_ENTITY;

		warn("GetPlayerByID: no player defined for id '"+id+"'");
		return INVALID_ENTITY;
	}

	RemoveLastPlayer()
	{
		if (!this.playerEntities.length)
			return;

		const lastId = this.playerEntities.pop();
		Engine.BroadcastMessage(MT_PlayerEntityChanged, {
			"player": this.playerEntities.length + 1,
			"from": lastId,
			"to": INVALID_ENTITY
		});
		Engine.DestroyEntity(lastId);
	}

	AddPlayer(templateName)
	{
		const ent = Engine.AddEntity(templateName);
		const id = this.playerEntities.length;
		const cmpPlayer = Engine.QueryInterface(ent, IID_Player);
		cmpPlayer.SetPlayerID(id);
		this.playerEntities.push(ent);

		Engine.BroadcastMessage(MT_PlayerEntityChanged, {
			"player": id,
			"from": INVALID_ENTITY,
			"to": ent
		});

		return id;
	}
}

PlayerManager.prototype.Schema = "<a:component type='system'/><empty/>";
Engine.RegisterSystemComponentType(IID_PlayerManager, "PlayerManager", PlayerManager);
