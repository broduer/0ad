const INVADING_PLAYERS_DATA = new Map([
	[2, {
		"targetPositionShip": { "x": 631.2, "z": 216.6 },
		"unloadDelay": 4000,
		"targetPositionSoldiers": { "x": 711, "z": 349.8 }
	}],
	[3, {
		"targetPositionShip": { "x": 845.9, "z": 234.2 },
		"unloadDelay": 9500,
		"targetPositionSoldiers": { "x": 800.7, "z": 353.4 }
	}],
	[4, {
		"targetPositionShip": { "x": 713.1, "z": 220.2 },
		"unloadDelay": 7500,
		"targetPositionSoldiers": { "x": 755, "z": 384 }
	}]
]);

const INVASION_FORCE_PER_PLAYER = {
	"units/rome/support_civilian": 4,
	"units/rome/infantry_spearman_b": 2,
	"units/rome/infantry_javelineer_b": 2,
	"units/rome/cavalry_spearman_b": 1
};

Trigger.prototype.PlayOpeningCutscene = function()
{
	const cmpCinemaInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaInterface);
	if (!cmpCinemaInterface)
		return;

	cmpCinemaInterface.PlayPathsWithSpeechSequence([
		{
			"paths": ["pathRomans"],
			"lines": [markForTranslation("In AD 43 began the Roman conquest of Britain. Emperor Claudius has launched a massive invasion from Normandy. Three divisions lead by Aulus Plautius have landed on the Southern shores of the British Isles.")]
		},
		{
			"paths": ["pathBritons"],
			"lines": [markForTranslation("But the Britons are fierce fighters, and are not willing to merely give up their land...")]
		}
	]);

	const invaderEnts = {};
	for (const player of INVADING_PLAYERS_DATA.keys())
	{
		const ents = { "ship": TriggerHelper.GetPlayerEntitiesByClass(player, ["Ship"])[0], "soldiers": [] };
		for (const [template, count] of Object.entries(INVASION_FORCE_PER_PLAYER))
			ents.soldiers = ents.soldiers.concat(TriggerHelper.SpawnGarrisonedUnits(ents.ship, template, count, player));
		invaderEnts[player] = ents;
	}

	const moveShipTo = (player, x, z) =>
	{
		ProcessCommand(player, {
			"type": "walk",
			"entities": [invaderEnts[player].ship],
			"x": x,
			"z": z,
		});
	};

	for (const [player, data] of INVADING_PLAYERS_DATA.entries())
	{
		// Make the ship sail to the coast, showing the arriving invaders.
		moveShipTo(player, data.targetPositionShip.x, data.targetPositionShip.z);
		// Once the ship has arrived, ungarrison the units and order them to walk inland.
		this.DoAfterDelay(data.unloadDelay, "UnloadAndMoveSoldiers", {
			"player": player,
			"ents": invaderEnts[player],
			"targetPosition": data.targetPositionSoldiers
		});
	}
};

Trigger.prototype.UnloadAndMoveSoldiers = function(data)
{
	ProcessCommand(data.player, {
		"type": "unload-all",
		"garrisonHolders": [data.ents.ship]
	});
	ProcessCommand(data.player, {
		"type": "walk",
		"entities": data.ents.soldiers,
		"x": data.targetPosition.x,
		"z": data.targetPosition.z
	});
};

Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger).RegisterTrigger("OnInitGame", "PlayOpeningCutscene", { "enabled": true });
