class Player
{
	Init()
	{
		this.playerID = undefined;
		this.color = undefined;
		this.state = this.STATE_ACTIVE;
		this.startCam = undefined;
	}

	GetDisplayedColor()
	{
		return this.color;
	}

	GetStartingCameraPos()
	{
		return this.startCam.position;
	}

	GetStartingCameraRot()
	{
		return this.startCam.rotation;
	}

	HasStartingCamera()
	{
		return this.startCam != undefined;
	}

	GetState()
	{
		// IsGameFinished Game.cpp code recieve "won" state for this component
		return this.state;
	}

	SetStartingCamera(pos, rot)
	{
		this.startCam = { "position": pos, "rotation": rot };
	}

	SetDisplayedColor(newColor)
	{
		this.color = newColor;
	}

	SetPlayerID(newId)
	{
		this.playerID = newId;
	}

	GetPlayerID()
	{
		return this.playerID;
	}

	SetColor(r, g, b)
	{
		this.color = { "r": r / 255, "g": g / 255, "b": b / 255, "a": 1 };
	}

	Serialize()
	{
		const state = {};
		for (const key in this)
			if (Object.prototype.hasOwnProperty.call(this, key))
				state[key] = this[key];
		return state;
	}

	Deserialize(state)
	{
		for (const prop in state)
			this[prop] = state[prop];
	}
}

Player.prototype.Schema = "<empty />";

Player.prototype.STATE_ACTIVE = "active";
Player.prototype.STATE_DEFEATED = "defeated";
Player.prototype.STATE_WON = "won";
Engine.RegisterComponentType(IID_Player, "Player", Player);
