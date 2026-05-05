Trigger.prototype.tutorialSteps = [
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Welcome to the 0\xa0A.D. tutorial."),
			"hint": markForTranslation("Click to start the tutorial."),
			"continueButton": markForTranslation("Start")
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Left-click on a Civilian and then right-click on a berry bush to make that Civilian gather food. Civilians gather vegetables faster than other units.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "gather" && msg.cmd.target &&
                TriggerHelper.GetResourceType(msg.cmd.target).specific == "fruit")
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Select the Citizen Soldier, right-click on a tree near the Civic Center to begin gathering wood. Citizen Soldiers gather wood faster than Civilians.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "gather" && msg.cmd.target &&
                TriggerHelper.GetResourceType(msg.cmd.target).specific == "tree")
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Select the Civic Center building and hold %(hotkey)s while clicking on the Hoplite icon once to begin training a batch of Hoplites."),
			"hotkeys": ["session.batchtrain"]
		},
		"Init": function()
		{
			this.trainingDone = false;
		},
		"OnTrainingQueued": function(msg)
		{
			if (msg.unitTemplate != "units/spart/infantry_spearman_b" || +msg.count == 1)
			{
				const cmpProductionQueue = Engine.QueryInterface(msg.trainerEntity, IID_ProductionQueue);
				cmpProductionQueue.ResetQueue();
				const txt = +msg.count == 1 ?
					markForTranslation("Do not forget to press the batch training hotkey while clicking to produce multiple units.") :
					markForTranslation("Click on the Hoplite icon.");
				this.DisplayWarning(txt);
				return;
			}
			this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Select the two idle Civilians and build a House nearby by selecting the House icon. Place the House by left-clicking on a piece of land.")
		},
		"OnTrainingFinished": function(msg)
		{
			this.trainingDone = true;
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "House"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Wait for the training of the Hoplites to finish.")
		},
		"IsDone": function()
		{
			return this.trainingDone;
		},
		"OnTrainingFinished": function(msg)
		{
			this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Select the newly trained Hoplites and assign them to build a Storehouse beside some nearby trees. They will begin to gather wood when it's constructed.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Storehouse"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Train a batch of Skirmishers by holding %(hotkey)s and clicking on the Skirmisher icon in the Civic Center."),
			"hotkeys": ["session.batchtrain"]
		},
		"Init": function()
		{
			this.trainingDone = false;
		},
		"OnTrainingQueued": function(msg)
		{
			if (msg.unitTemplate != "units/spart/infantry_javelineer_b" || +msg.count == 1)
			{
				const cmpProductionQueue = Engine.QueryInterface(msg.trainerEntity, IID_ProductionQueue);
				cmpProductionQueue.ResetQueue();
				const txt = +msg.count == 1 ?
					markForTranslation("Do not forget to press the batch training hotkey while clicking to produce multiple units.") :
					markForTranslation("Click on the Skirmisher icon.");
				this.DisplayWarning(txt);
				return;
			}
			this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Build a Farmstead in an open space beside the Civic Center using any idle builders.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Farmstead"))
				this.NextStep();
		},
		"OnTrainingFinished": function(msg)
		{
			this.trainingDone = true;
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Let's wait for them to finish building the Farmstead, so they can use it as a dropsite."),
			"hint": markForTranslation("Wait for the Farmstead to be built.")
		},
		"OnTrainingFinished": function(msg)
		{
			this.trainingDone = true;
		},
		"OnStructureBuilt": function(msg)
		{
			if (TriggerHelper.EntityMatchesClassList(msg.building, "Farmstead"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Now its builders will automatically begin gathering food if there is any nearby. Select the builders and instead make them construct a Field beside the Farmstead.")
		},
		"Init": function()
		{
			this.farmStarted = false;
		},
		"IsDone": function()
		{
			return this.farmStarted && this.trainingDone;
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Field"))
				this.farmStarted = true;
			if (this.IsDone())
				this.NextStep();
		},
		"OnTrainingFinished": function(msg)
		{
			this.trainingDone = true;
			if (this.IsDone())
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("The Field's builders will now automatically begin gathering food from the Field. Using the newly created group of skirmishers, get them to build another House nearby.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "House"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Train a batch of Hoplites at the Civic Center. Select the Civic Center and with it selected right-click on a tree nearby. Units from the Civic Center will now automatically gather wood.")
		},
		"Init": function()
		{
			this.rallyPointSet = false;
			this.trainingStarted = false;
		},
		"IsDone": function()
		{
			return this.rallyPointSet && this.trainingStarted;
		},
		"OnTrainingQueued": function(msg)
		{
			if (msg.unitTemplate != "units/spart/infantry_spearman_b" || +msg.count == 1)
			{
				const cmpProductionQueue = Engine.QueryInterface(msg.trainerEntity, IID_ProductionQueue);
				cmpProductionQueue.ResetQueue();
				const txt = +msg.count == 1 ?
					markForTranslation("Do not forget to press the batch training hotkey while clicking to produce multiple units.") :
					markForTranslation("Click on the Hoplite icon.");
				this.DisplayWarning(txt);
				return;
			}
			this.trainingStarted = true;
			if (this.IsDone())
				this.NextStep();
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type != "set-rallypoint" || !msg.cmd.data ||
               !msg.cmd.data.command || msg.cmd.data.command != "gather" ||
               !msg.cmd.data.resourceType || msg.cmd.data.resourceType.specific != "tree")
			{
				this.DisplayWarning(markForTranslation("Select the Civic Center, then hover the cursor over the tree and right-click when you see your cursor change into a wood icon."));
				return;
			}
			this.rallyPointSet = true;
			if (this.IsDone())
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Build a Barracks nearby. Whenever your population limit is reached, build an extra House using any available builder units.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Barracks"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("The Barracks will be your fifth Village Phase structure, allowing you to advance to the Town Phase."),
			"hint": markForTranslation("Wait for the Barracks to be built.")
		},
		"OnStructureBuilt": function(msg)
		{
			if (TriggerHelper.EntityMatchesClassList(msg.building, "Barracks"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Select the Civic Center again and advance to Town Phase by clicking on the II icon (you have to wait for the barracks to be built first). This will allow Town Phase buildings to be constructed.")
		},
		"IsDone": function()
		{
			return TriggerHelper.HasDealtWithTech(this.playerID, "phase_town_generic");
		},
		"OnResearchQueued": function(msg)
		{
			if (msg.technologyTemplate && TriggerHelper.EntityMatchesClassList(msg.researcherEntity, "CivilCentre"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("While waiting for the phasing up, you may reassign your idle workers to gathering the resources you are short of."),
			"hint": markForTranslation("Wait until you reach the Town Phase.")
		},
		"IsDone": function()
		{
			const cmpPlayerManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_PlayerManager);
			const playerEnt = cmpPlayerManager.GetPlayerByID(this.playerID);
			const cmpTechnologyManager = Engine.QueryInterface(playerEnt, IID_TechnologyManager);
			return cmpTechnologyManager && cmpTechnologyManager.IsTechnologyResearched("phase_town_generic");
		},
		"OnResearchFinished": function(msg)
		{
			if (msg.tech == "phase_town_generic")
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Order the idle Skirmishers to build an outpost to the north east at the edge of your territory.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Outpost"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Start training a batch of Civilians in the Civic Center and set its rally point to the Field (right click on it).")
		},
		"Init": function()
		{
			this.rallyPointSet = false;
			this.trainingStarted = false;
		},
		"IsDone": function()
		{
			return this.rallyPointSet && this.trainingStarted;
		},
		"OnTrainingQueued": function(msg)
		{
			if (msg.unitTemplate != "units/spart/support_civilian" || +msg.count == 1)
			{
				const cmpProductionQueue = Engine.QueryInterface(msg.trainerEntity, IID_ProductionQueue);
				cmpProductionQueue.ResetQueue();
				const txt = +msg.count == 1 ?
					markForTranslation("Do not forget to press the batch training hotkey while clicking to produce multiple units.") :
					markForTranslation("Click on the Civilian icon.");
				this.DisplayWarning(txt);
				return;
			}
			this.trainingStarted = true;
			if (this.IsDone())
				this.NextStep();
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type != "set-rallypoint" || !msg.cmd.data ||
               !msg.cmd.data.command || msg.cmd.data.command != "gather" ||
               !msg.cmd.data.resourceType || msg.cmd.data.resourceType.specific != "grain")
				return;
			this.rallyPointSet = true;
			if (this.IsDone())
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Prepare for an attack by an enemy player. Train more soldiers using the Barracks, and get idle soldiers to build a Tower near your Outpost.")
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type == "repair" && TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Tower"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Build a Forge and research the Infantry Training technology (sword icon) to improve infantry hack attack.")
		},
		"OnResearchQueued": function(msg)
		{
			if (msg.technologyTemplate && TriggerHelper.EntityMatchesClassList(msg.researcherEntity, "Forge"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("The enemy is coming. Train more soldiers to fight off the enemies.")
		},
		"OnResearchFinished": function(msg)
		{
			this.LaunchAttack();
			this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Try to repel the attack.")
		},
		"OnOwnershipChanged": function(msg)
		{
			if (msg.to != INVALID_PLAYER)
				return;
			if (this.IsAttackRepelled())
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("The enemy attack has been thwarted. Now build a market and a temple while you assign new units to gather required resources.")
		},
		"Init": function()
		{
			this.marketStarted = false;
			this.templeStarted = false;
		},
		"IsDone": function()
		{
			return this.marketStarted && this.templeStarted;
		},
		"OnPlayerCommand": function(msg)
		{
			if (msg.cmd.type != "repair")
				return;
			this.marketStarted = this.marketStarted || TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Market");
			this.templeStarted = this.templeStarted || TriggerHelper.EntityMatchesClassList(msg.cmd.target, "Temple");
			if (this.IsDone())
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Once you meet the City Phase requirements, select your Civic Center and advance to City Phase.")
		},
		"IsDone": function()
		{
			return TriggerHelper.HasDealtWithTech(this.playerID, "phase_city_generic");
		},
		"OnResearchQueued": function(msg)
		{
			if (msg.technologyTemplate && TriggerHelper.EntityMatchesClassList(msg.researcherEntity, "CivilCentre"))
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("While waiting for the phase change, you may train more soldiers at the Barracks."),
			"hint": markForTranslation("Wait until you reach the City Phase")
		},
		"IsDone": function()
		{
			return TriggerHelper.HasDealtWithTech(this.playerID, "phase_city_generic");
		},
		"OnResearchFinished": function(msg)
		{
			if (msg.tech == "phase_city_generic")
				this.NextStep();
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text": markForTranslation("Now that you are in City Phase, build the Arsenal nearby and then use it to construct 2 Battering Rams.")
		},
		"Init": function()
		{
			this.ramCount = 0;
		},
		"IsDone": function()
		{
			return this.ramCount > 1;
		},
		"OnTrainingQueued": function(msg)
		{
			if (msg.unitTemplate == "units/spart/siege_ram")
				this.ramCount += msg.count;
			if (this.IsDone())
			{
				this.RemoveChampions();
				this.NextStep();
			}
		}
	},
	{
		"type": TUTORIAL_STEP_TYPE.INSTRUCTION,
		"panelData": {
			"text":
                markForTranslation("Stop all your soldiers gathering resources and instead task small groups to find the enemy Civic Center on the map. Once the enemy's base has been spotted, send your Siege Engines and all remaining soldiers to destroy it.\n") +
                markForTranslation("Civilians should continue to gather resources.")
		},
		"OnOwnershipChanged": function(msg)
		{
			if (msg.from != this.enemyID)
				return;
			if (TriggerHelper.EntityMatchesClassList(msg.entity, "CivilCentre"))
				this.CompleteTutorial(markForTranslation("You have defeated the enemy by destroying or capturing the Civic Center. These tutorial tasks are now completed."));
		}
	}
];

Trigger.prototype.LaunchAttack = function()
{
	const cmpRangeManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_RangeManager);
	const entities = cmpRangeManager.GetEntitiesByPlayer(this.playerID);
	const target =
		entities.find(e =>
		{
			const cmpIdentity = Engine.QueryInterface(e, IID_Identity);
			return cmpIdentity && cmpIdentity.HasClass("Tower") && Engine.QueryInterface(e, IID_Position);
		}) ||
		entities.find(e =>
		{
			const cmpIdentity = Engine.QueryInterface(e, IID_Identity);
			return cmpIdentity && cmpIdentity.HasClass("CivilCentre") && Engine.QueryInterface(e, IID_Position);
		});

	const position = Engine.QueryInterface(target, IID_Position).GetPosition2D();

	this.attackers = cmpRangeManager.GetEntitiesByPlayer(this.enemyID).filter(e =>
	{
		const cmpIdentity = Engine.QueryInterface(e, IID_Identity);
		return Engine.QueryInterface(e, IID_UnitAI) && cmpIdentity && cmpIdentity.HasClass("CitizenSoldier");
	});

	ProcessCommand(this.enemyID, {
		"type": "attack-walk",
		"entities": this.attackers,
		"x": position.x,
		"z": position.y,
		"targetClasses": { "attack": ["Unit"] },
		"allowCapture": false,
		"queued": false
	});
};

Trigger.prototype.IsAttackRepelled = function()
{
	return !this.attackers.some(e => Engine.QueryInterface(e, IID_Health) && Engine.QueryInterface(e, IID_Health).GetHitpoints() > 0);
};

Trigger.prototype.RemoveChampions = function()
{
	const cmpRangeManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_RangeManager);
	const champions = cmpRangeManager.GetEntitiesByPlayer(this.enemyID).filter(e => Engine.QueryInterface(e, IID_Identity).HasClass("Champion"));
	let keep = 6;
	for (const ent of champions)
	{
		const cmpHealth = Engine.QueryInterface(ent, IID_Health);
		if (!cmpHealth)
			Engine.DestroyEntity(ent);
		else if (--keep < 0)
			cmpHealth.Kill();
	}
};

{
	const cmpTrigger = Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger);
	cmpTrigger.playerID = 1;
	cmpTrigger.enemyID = 2;
	cmpTrigger.RegisterTrigger("OnInitGame", "InitTutorial", { "enabled": true });
}
