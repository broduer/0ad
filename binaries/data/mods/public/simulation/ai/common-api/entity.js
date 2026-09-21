import { VectorDistance } from "simulation/ai/common-api/utils.js";

// defines a template.
export class Template
{
	constructor(sharedAI, templateName, template)
	{
		this._templateName = templateName;
		this._template = template;
		// save a reference to the template tech modifications
		if (!sharedAI._templatesModifications[this._templateName])
			sharedAI._templatesModifications[this._templateName] = {};
		this._templateModif = sharedAI._templatesModifications[this._templateName];
		this._tpCache = new Map();
	}

	// Helper function to return a template value, adjusting for tech.
	get(string)
	{
		if (this._entityModif && this._entityModif.has(string))
			return this._entityModif.get(string);
		else if (this._templateModif)
		{
			const owner = this._entity ? this._entity.owner : PlayerID;
			if (this._templateModif[owner] && this._templateModif[owner].has(string))
				return this._templateModif[owner].get(string);
		}

		if (!this._tpCache.has(string))
		{
			let value = this._template;
			const args = string.split("/");
			for (const arg of args)
			{
				value = value[arg];
				if (value == undefined)
					break;
			}
			this._tpCache.set(string, value);
		}
		return this._tpCache.get(string);
	}

	templateName() { return this._templateName; }

	genericName() { return this.get("Identity/GenericName"); }

	civ() { return this.get("Identity/Civ"); }

	matchLimit()
	{
		if (!this.get("TrainingRestrictions"))
			return undefined;
		return this.get("TrainingRestrictions/MatchLimit");
	}

	classes()
	{
		const template = this.get("Identity");
		if (!template)
			return undefined;
		return GetIdentityClasses(template);
	}

	hasClass(name)
	{
		if (!this._classes)
			this._classes = this.classes();
		return this._classes && this._classes.indexOf(name) != -1;
	}

	hasClasses(array)
	{
		if (!this._classes)
			this._classes = this.classes();
		return this._classes && MatchesClassList(this._classes, array);
	}

	requirements()
	{
		return this.get("Identity/Requirements");
	}

	available(gameState)
	{
		const requirements = this.requirements();
		return !requirements || Sim.RequirementsHelper.AreRequirementsMet(requirements, PlayerID);
	}

	cost(productionQueue)
	{
		if (!this.get("Cost"))
			return {};

		const ret = {};
		for (const type in this.get("Cost/Resources"))
			ret[type] = +this.get("Cost/Resources/" + type);
		return ret;
	}

	costSum(productionQueue)
	{
		const cost = this.cost(productionQueue);
		if (!cost)
			return 0;
		let ret = 0;
		for (const type in cost)
			ret += cost[type];
		return ret;
	}

	techCostMultiplier(type)
	{
		return +(this.get("Researcher/TechCostMultiplier/"+type) || 1);
	}

	/**
	 * Returns { "max": max, "min": min } or undefined if no obstruction.
	 * max: radius of the outer circle surrounding this entity's obstruction shape
	 * min: radius of the inner circle
	 */
	obstructionRadius()
	{
		if (!this.get("Obstruction"))
			return undefined;

		if (this.get("Obstruction/Static"))
		{
			const w = +this.get("Obstruction/Static/@width");
			const h = +this.get("Obstruction/Static/@depth");
			return { "max": Math.sqrt(w * w + h * h) / 2, "min": Math.min(h, w) / 2 };
		}

		if (this.get("Obstruction/Unit"))
		{
			const r = +this.get("Obstruction/Unit/@radius");
			return { "max": r, "min": r };
		}

		const right = this.get("Obstruction/Obstructions/Right");
		const left = this.get("Obstruction/Obstructions/Left");
		if (left && right)
		{
			const w = +right["@x"] + right["@width"] / 2 - left["@x"] + left["@width"] / 2;
			const h = Math.max(+right["@z"] + right["@depth"] / 2, +left["@z"] + left["@depth"] / 2) -
			        Math.min(+right["@z"] - right["@depth"] / 2, +left["@z"] - left["@depth"] / 2);
			return { "max": Math.sqrt(w * w + h * h) / 2, "min": Math.min(h, w) / 2 };
		}

		return { "max": 0, "min": 0 }; // Units have currently no obstructions
	}

	/**
	 * Returns the radius of a circle surrounding this entity's footprint.
	 */
	footprintRadius()
	{
		if (!this.get("Footprint"))
			return undefined;

		if (this.get("Footprint/Square"))
		{
			const w = +this.get("Footprint/Square/@width");
			const h = +this.get("Footprint/Square/@depth");
			return Math.sqrt(w * w + h * h) / 2;
		}

		if (this.get("Footprint/Circle"))
			return +this.get("Footprint/Circle/@radius");

		return 0; // this should never happen
	}

	maxHitpoints() { return +(this.get("Health/Max") || 0); }

	isHealable()
	{
		if (this.get("Health") !== undefined)
			return this.get("Health/Unhealable") !== "true";
		return false;
	}

	isRepairable() { return this.get("Repairable") !== undefined; }

	getPopulationBonus()
	{
		if (!this.get("Population"))
			return 0;

		return +this.get("Population/Bonus");
	}

	resistanceStrengths()
	{
		const resistanceTypes = this.get("Resistance");
		if (!resistanceTypes || !resistanceTypes.Entity)
			return undefined;

		const resistance = {};
		if (resistanceTypes.Entity.Capture)
			resistance.Capture = +this.get("Resistance/Entity/Capture");

		if (resistanceTypes.Entity.Damage)
		{
			resistance.Damage = {};
			for (const damageType in resistanceTypes.Entity.Damage)
				resistance.Damage[damageType] = +this.get("Resistance/Entity/Damage/" + damageType);
		}

		// ToDo: Resistance to StatusEffects.

		return resistance;
	}

	attackTypes()
	{
		const attack = this.get("Attack");
		if (!attack)
			return undefined;

		// Charge is dealt by moving through units, not an attack that can be ordered.
		const ret = [];
		for (const type in attack)
			if (type != "ChargeAttack")
				ret.push(type);
		return ret;
	}

	attackRange(type)
	{
		if (!this.get("Attack/" + type))
			return undefined;

		return {
			"max": +this.get("Attack/" + type +"/MaxRange"),
			"min": +(this.get("Attack/" + type +"/MinRange") || 0)
		};
	}

	attackStrengths(type)
	{
		const attackDamageTypes = this.get("Attack/" + type + "/Damage");
		if (!attackDamageTypes)
			return undefined;

		const damage = {};
		for (const damageType in attackDamageTypes)
			damage[damageType] = +attackDamageTypes[damageType];

		return damage;
	}

	captureStrength()
	{
		if (!this.get("Attack/Capture"))
			return undefined;

		return +this.get("Attack/Capture/Capture") || 0;
	}

	attackTimes(type)
	{
		if (!this.get("Attack/" + type))
			return undefined;

		return {
			"prepare": +(this.get("Attack/" + type + "/PrepareTime") || 0),
			"repeat": +(this.get("Attack/" + type + "/RepeatTime") || 1000)
		};
	}

	// returns the classes this templates counters:
	// Return type is [ [-neededClasses- , multiplier], … ].
	getCounteredClasses()
	{
		const attack = this.get("Attack");
		if (!attack)
			return undefined;

		const Classes = [];
		for (const type in attack)
		{
			const bonuses = this.get("Attack/" + type + "/Bonuses");
			if (!bonuses)
				continue;
			for (const b in bonuses)
			{
				const bonusClasses = this.get("Attack/" + type + "/Bonuses/" + b + "/Classes");
				if (bonusClasses)
					Classes.push([bonusClasses.split(" "), +this.get("Attack/" + type +"/Bonuses/" + b +"/Multiplier")]);
			}
		}
		return Classes;
	}

	// returns true if the entity counters the target entity.
	// TODO: refine using the multiplier
	counters(target)
	{
		const attack = this.get("Attack");
		if (!attack)
			return false;
		const mcounter = [];
		for (const type in attack)
		{
			const bonuses = this.get("Attack/" + type + "/Bonuses");
			if (!bonuses)
				continue;
			for (const b in bonuses)
			{
				const bonusClasses = this.get("Attack/" + type + "/Bonuses/" + b + "/Classes");
				if (bonusClasses)
					mcounter.concat(bonusClasses.split(" "));
			}
		}
		return target.hasClasses(mcounter);
	}

	// returns, if it exists, the multiplier from each attack against a given class
	getMultiplierAgainst(type, againstClass)
	{
		if (!this.get("Attack/" + type +""))
			return undefined;

		const bonuses = this.get("Attack/" + type + "/Bonuses");
		if (bonuses)
		{
			for (const b in bonuses)
			{
				const bonusClasses = this.get("Attack/" + type + "/Bonuses/" + b + "/Classes");
				if (!bonusClasses)
					continue;
				for (const bcl of bonusClasses.split(" "))
					if (bcl == againstClass)
						return +this.get("Attack/" + type + "/Bonuses/" + b + "/Multiplier");
			}
		}
		return 1;
	}

	buildableEntities(civ)
	{
		const templates = this.get("Builder/Entities/_string");
		if (!templates)
			return [];
		return templates.replace(/\{native\}/g, this.civ()).replace(/\{civ\}/g, civ).split(/\s+/);
	}

	trainableEntities(civ)
	{
		const templates = this.get("Trainer/Entities/_string");
		if (!templates)
			return undefined;
		return templates.replace(/\{native\}/g, this.civ()).replace(/\{civ\}/g, civ).split(/\s+/);
	}

	researchableTechs(gameState, civ)
	{
		const templates = this.get("Researcher/Technologies/_string");
		if (!templates)
			return undefined;
		const techs = templates.split(/\s+/);
		for (let i = 0; i < techs.length; ++i)
		{
			const tech = techs[i];
			if (tech.indexOf("{civ}") == -1)
				continue;
			const civTech = tech.replace("{civ}", civ);
			techs[i] = TechnologyTemplates.Has(civTech) ?
				civTech : tech.replace("{civ}", "generic");
		}
		return techs;
	}

	resourceSupplyType()
	{
		if (!this.get("ResourceSupply"))
			return undefined;
		const [type, subtype] = this.get("ResourceSupply/Type").split('.');
		return { "generic": type, "specific": subtype };
	}

	getResourceType()
	{
		if (!this.get("ResourceSupply"))
			return undefined;
		return this.get("ResourceSupply/Type").split('.')[0];
	}

	getDiminishingReturns() { return +(this.get("ResourceSupply/DiminishingReturns") || 1); }

	resourceSupplyMax() { return +this.get("ResourceSupply/Max"); }

	maxGatherers() { return +(this.get("ResourceSupply/MaxGatherers") || 0); }

	resourceGatherRates()
	{
		if (!this.get("ResourceGatherer"))
			return undefined;
		const ret = {};
		const baseSpeed = +this.get("ResourceGatherer/BaseSpeed");
		for (const r in this.get("ResourceGatherer/Rates"))
			ret[r] = +this.get("ResourceGatherer/Rates/" + r) * baseSpeed;
		return ret;
	}

	resourceDropsiteTypes()
	{
		if (!this.get("ResourceDropsite"))
			return undefined;

		const types = this.get("ResourceDropsite/Types");
		return types ? types.split(/\s+/) : [];
	}

	isResourceDropsite(resourceType)
	{
		const types = this.resourceDropsiteTypes();
		return types && (!resourceType || types.indexOf(resourceType) !== -1);
	}

	isTreasure() { return this.get("Treasure") !== undefined; }

	treasureResources()
	{
		if (!this.get("Treasure"))
			return undefined;
		const ret = {};
		for (const r in this.get("Treasure/Resources"))
			ret[r] = +this.get("Treasure/Resources/" + r);
		return ret;
	}


	garrisonableClasses() { return this.get("GarrisonHolder/List/_string"); }

	garrisonMax() { return this.get("GarrisonHolder/Max"); }

	garrisonSize() { return this.get("Garrisonable/Size"); }

	garrisonEjectHealth() { return +this.get("GarrisonHolder/EjectHealth"); }

	getDefaultArrow() { return +this.get("BuildingAI/DefaultArrowCount"); }

	getArrowMultiplier() { return +this.get("BuildingAI/GarrisonArrowMultiplier"); }

	getGarrisonArrowClasses()
	{
		if (!this.get("BuildingAI"))
			return undefined;
		return this.get("BuildingAI/GarrisonArrowClasses").split(/\s+/);
	}

	buffHeal() { return +this.get("GarrisonHolder/BuffHeal"); }

	promotion() { return this.get("Promotion/Entity"); }

	isPackable() { return this.get("Pack") != undefined; }

	isHuntable()
	{
		// Do not hunt retaliating animals (dead animals can be used).
		// Assume entities which can attack, will attack.
		return this.get("ResourceSupply/KillBeforeGather") &&
			(!this.get("Health") || !this.get("Attack"));
	}

	walkSpeed() { return +this.get("UnitMotion/WalkSpeed"); }

	trainingCategory() { return this.get("TrainingRestrictions/Category"); }

	buildTime(researcher)
	{
		let time = +this.get("Cost/BuildTime");
		if (researcher)
			time *= researcher.techCostMultiplier("time");
		return time;
	}

	buildCategory() { return this.get("BuildRestrictions/Category"); }

	buildDistance()
	{
		const distance = this.get("BuildRestrictions/Distance");
		if (!distance)
			return undefined;
		const ret = {};
		for (const key in distance)
			ret[key] = this.get("BuildRestrictions/Distance/" + key);
		return ret;
	}

	buildPlacementType() { return this.get("BuildRestrictions/PlacementType"); }

	buildTerritories()
	{
		if (!this.get("BuildRestrictions"))
			return undefined;
		const territory = this.get("BuildRestrictions/Territory");
		return !territory ? undefined : territory.split(/\s+/);
	}

	hasBuildTerritory(territory)
	{
		const territories = this.buildTerritories();
		return territories && territories.indexOf(territory) != -1;
	}

	hasTerritoryInfluence()
	{
		return this.get("TerritoryInfluence") !== undefined;
	}

	hasDefensiveFire()
	{
		if (!this.get("Attack/Ranged"))
			return false;
		return this.getDefaultArrow() || this.getArrowMultiplier();
	}

	territoryInfluenceRadius()
	{
		if (this.get("TerritoryInfluence") !== undefined)
			return +this.get("TerritoryInfluence/Radius");
		return -1;
	}

	territoryInfluenceWeight()
	{
		if (this.get("TerritoryInfluence") !== undefined)
			return +this.get("TerritoryInfluence/Weight");
		return -1;
	}

	territoryDecayRate()
	{
		return +(this.get("TerritoryDecay/DecayRate") || 0);
	}

	defaultRegenRate()
	{
		return +(this.get("Capturable/RegenRate") || 0);
	}

	garrisonRegenRate()
	{
		return +(this.get("Capturable/GarrisonRegenRate") || 0);
	}

	visionRange() { return +this.get("Vision/Range"); }

	gainMultiplier() { return +this.get("Trader/GainMultiplier"); }

	isBuilder() { return this.get("Builder") !== undefined; }

	isGatherer() { return this.get("ResourceGatherer") !== undefined; }

	canGather(type)
	{
		const gatherRates = this.get("ResourceGatherer/Rates");
		if (!gatherRates)
			return false;
		for (const r in gatherRates)
			if (r.split('.')[0] === type)
				return true;
		return false;
	}

	isGarrisonHolder() { return this.get("GarrisonHolder") !== undefined; }

	isTurretHolder() { return this.get("TurretHolder") !== undefined; }

	/**
	 * returns true if the tempalte can capture the given target entity
	 * if no target is given, returns true if the template has the Capture attack
	 */
	canCapture(target)
	{
		if (!this.get("Attack/Capture"))
			return false;
		if (!target)
			return true;
		if (!target.get("Capturable"))
			return false;
		const restrictedClasses = this.get("Attack/Capture/RestrictedClasses/_string");
		return !restrictedClasses || !target.hasClasses(restrictedClasses);
	}

	isCapturable() { return this.get("Capturable") !== undefined; }

	canGuard() { return this.get("UnitAI/CanGuard") === "true"; }

	canGarrison() { return "Garrisonable" in this._template; }

	canOccupyTurret() { return "Turretable" in this._template; }

	isTreasureCollector() { return this.get("TreasureCollector") !== undefined; }

	hasUnitAI() { return this.get("UnitAI") !== undefined; }
}


// defines an entity, with a super Template.
// also redefines several of the template functions where the only change is applying aura and tech modifications.
export class Entity extends Template
{
	constructor(sharedAI, entity)
	{
		super(sharedAI, entity.template, sharedAI.GetTemplate(entity.template));

		this._entity = entity;
		this._ai = sharedAI;
		// save a reference to the template tech modifications
		if (!sharedAI._templatesModifications[this._templateName])
			sharedAI._templatesModifications[this._templateName] = {};
		this._templateModif = sharedAI._templatesModifications[this._templateName];
		// save a reference to the entity tech/aura modifications
		if (!sharedAI._entitiesModifications.has(entity.id))
			sharedAI._entitiesModifications.set(entity.id, new Map());
		this._entityModif = sharedAI._entitiesModifications.get(entity.id);
	}

	queryInterface(iid) { return SimEngine.QueryInterface(this.id(), iid); }

	toString() { return "[Entity " + this.id() + " " + this.templateName() + "]"; }

	id() { return this._entity.id; }

	/**
	 * Returns extra data that the AI scripts have associated with this entity,
	 * for arbitrary local annotations.
	 * (This data should not be shared with any other AI scripts.)
	 */
	getMetadata(player, key) { return this._ai.getMetadata(player, this, key); }

	/**
	 * Sets extra data to be associated with this entity.
	 */
	setMetadata(player, key, value) { this._ai.setMetadata(player, this, key, value); }

	deleteAllMetadata(player) { delete this._ai._entityMetadata[player][this.id()]; }

	deleteMetadata(player, key) { this._ai.deleteMetadata(player, this, key); }

	position() { return this._entity.position; }
	angle() { return this._entity.angle; }

	isIdle() { return this._entity.idle; }

	getStance() { return this._entity.stance; }
	unitAIState() { return this._entity.unitAIState; }
	unitAIOrderData() { return SimEngine.QueryInterface(this.id(), Sim.IID_UnitAI).GetOrders(); }

	hitpoints() { return this._entity.hitpoints; }
	isHurt() { return this.hitpoints() < this.maxHitpoints(); }
	healthLevel() { return this.hitpoints() / this.maxHitpoints(); }
	needsHeal() { return this.isHurt() && this.isHealable(); }
	needsRepair() { return this.isHurt() && this.isRepairable(); }
	decaying() { return this._entity.decaying; }
	capturePoints() {return this._entity.capturePoints; }
	isInvulnerable() { return this._entity.invulnerability || false; }

	isSharedDropsite() { return this._entity.sharedDropsite === true; }

	/**
	 * Returns the current training queue state, of the form
	 * [ { "id": 0, "template": "...", "count": 1, "progress": 0.5, "metadata": ... }, ... ]
	 */
	trainingQueue()
	{
		return this._entity.trainingQueue;
	}

	trainingQueueTime()
	{
		const queue = this._entity.trainingQueue;
		if (!queue)
			return undefined;
		let time = 0;
		for (const item of queue)
			time += item.timeRemaining;
		return time / 1000;
	}

	foundationProgress()
	{
		return this._entity.foundationProgress;
	}

	getBuilders()
	{
		if (this._entity.foundationProgress === undefined)
			return undefined;
		if (this._entity.foundationBuilders === undefined)
			return [];
		return this._entity.foundationBuilders;
	}

	getBuildersNb()
	{
		if (this._entity.foundationProgress === undefined)
			return undefined;
		if (this._entity.foundationBuilders === undefined)
			return 0;
		return this._entity.foundationBuilders.length;
	}

	owner()
	{
		return this._entity.owner;
	}

	isOwn(player)
	{
		if (typeof this._entity.owner === "undefined")
			return false;
		return this._entity.owner === player;
	}

	resourceSupplyAmount()
	{
		return this.queryInterface(Sim.IID_ResourceSupply)?.GetCurrentAmount();
	}

	resourceSupplyNumGatherers()
	{
		return this.queryInterface(Sim.IID_ResourceSupply)?.GetNumGatherers();
	}

	isFull()
	{
		const numGatherers = this.resourceSupplyNumGatherers();
		if (numGatherers)
			return this.maxGatherers() === numGatherers;

		return undefined;
	}

	resourceCarrying()
	{
		return this.queryInterface(Sim.IID_ResourceGatherer)?.GetCarryingStatus();
	}

	currentGatherRate()
	{
		// returns the gather rate for the current target if applicable.
		if (!this.get("ResourceGatherer"))
			return undefined;

		if (this.unitAIOrderData().length &&
			this.unitAIState().split(".")[1] == "GATHER")
		{
			let res;
			// this is an abuse of "_ai" but it works.
			if (this.unitAIState().split(".")[1] == "GATHER" && this.unitAIOrderData()[0].target !== undefined)
				res = this._ai._entities.get(this.unitAIOrderData()[0].target);
			else if (this.unitAIOrderData()[1] !== undefined && this.unitAIOrderData()[1].target !== undefined)
				res = this._ai._entities.get(this.unitAIOrderData()[1].target);
			if (!res)
				return 0;
			const type = res.resourceSupplyType();
			if (!type)
				return 0;

			const tstring = type.generic + "." + type.specific;
			let rate = +this.get("ResourceGatherer/BaseSpeed");
			rate *= +this.get("ResourceGatherer/Rates/" +tstring);
			if (rate)
				return rate;
			return 0;
		}
		return undefined;
	}

	garrisonHolderID()
	{
		return this._entity.garrisonHolderID;
	}

	garrisoned() { return this._entity.garrisoned; }

	garrisonedSlots()
	{
		let count = 0;

		if (this._entity.garrisoned)
			for (const ent of this._entity.garrisoned)
				count += +this._ai._entities.get(ent).garrisonSize();

		return count;
	}

	canGarrisonInside()
	{
		return this.garrisonedSlots() < this.garrisonMax();
	}

	/**
	 * returns true if the entity can attack (including capture) the given class.
	 */
	canAttackClass(aClass)
	{
		const attack = this.get("Attack");
		if (!attack)
			return false;

		for (const type in attack)
		{
			if (type == "Slaughter" || type == "ChargeAttack")
				continue;
			const restrictedClasses = this.get("Attack/" + type + "/RestrictedClasses/_string");
			if (!restrictedClasses || !MatchesClassList([aClass], restrictedClasses))
				return true;
		}
		return false;
	}

	/**
	 * Derived from Attack.js' similary named function.
	 * @return {boolean} - Whether an entity can attack a given target.
	 */
	canAttackTarget(target, allowCapture)
	{
		const attackTypes = this.get("Attack");
		if (!attackTypes)
			return false;

		const canCapture = allowCapture && this.canCapture(target);
		const health = target.get("Health");
		if (!health)
			return canCapture;

		for (const type in attackTypes)
		{
			if (type == "ChargeAttack")
				continue;
			if (type == "Capture" ? !canCapture : target.isInvulnerable())
				continue;

			const restrictedClasses = this.get("Attack/" + type + "/RestrictedClasses/_string");
			if (!restrictedClasses || !target.hasClasses(restrictedClasses))
				return true;
		}

		return false;
	}

	move(x, z, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "walk", "entities": [this.id()], "x": x, "z": z, "queued": queued, "pushFront": pushFront });
		return this;
	}

	moveToRange(x, z, min, max, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "walk-to-range", "entities": [this.id()], "x": x, "z": z, "min": min, "max": max, "queued": queued, "pushFront": pushFront });
		return this;
	}

	attackMove(x, z, targetClasses, allowCapture = true, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "attack-walk", "entities": [this.id()], "x": x, "z": z, "targetClasses": targetClasses, "allowCapture": allowCapture, "queued": queued, "pushFront": pushFront });
		return this;
	}

	// violent, aggressive, defensive, passive, standground
	setStance(stance)
	{
		if (this.getStance() === undefined)
			return undefined;
		Engine.PostCommand(PlayerID, { "type": "stance", "entities": [this.id()], "name": stance });
		return this;
	}

	stopMoving()
	{
		Engine.PostCommand(PlayerID, { "type": "stop", "entities": [this.id()], "queued": false, "pushFront": false });
	}

	unload(id)
	{
		if (!this.get("GarrisonHolder"))
			return undefined;
		Engine.PostCommand(PlayerID, { "type": "unload", "garrisonHolder": this.id(), "entities": [id] });
		return this;
	}

	// Unloads all owned units, don't unload allies
	unloadAll()
	{
		if (!this.get("GarrisonHolder"))
			return undefined;
		Engine.PostCommand(PlayerID, { "type": "unload-all-by-owner", "garrisonHolders": [this.id()] });
		return this;
	}

	garrison(target, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "garrison", "entities": [this.id()], "target": target.id(), "queued": queued, "pushFront": pushFront });
		return this;
	}

	["occupy-turret"](target, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "occupy-turret", "entities": [this.id()], "target": target.id(), "queued": queued, "pushFront": pushFront });
		return this;
	}

	attack(unitId, allowCapture = true, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "attack", "entities": [this.id()], "target": unitId, "allowCapture": allowCapture, "queued": queued, "pushFront": pushFront });
		return this;
	}

	collectTreasure(target, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, {
			"type": "collect-treasure",
			"entities": [this.id()],
			"target": target.id(),
			"queued": queued,
			"pushFront": pushFront
		});
		return this;
	}

	// moveApart from a point in the opposite direction with a distance dist
	moveApart(point, dist)
	{
		if (this.position() !== undefined)
		{
			let direction = [this.position()[0] - point[0], this.position()[1] - point[1]];
			const norm = VectorDistance(point, this.position());
			if (norm === 0)
				direction = [1, 0];
			else
			{
				direction[0] /= norm;
				direction[1] /= norm;
			}
			Engine.PostCommand(PlayerID, { "type": "walk", "entities": [this.id()], "x": this.position()[0] + direction[0]*dist, "z": this.position()[1] + direction[1]*dist, "queued": false, "pushFront": false });
		}
		return this;
	}

	// Flees from a unit in the opposite direction.
	flee(unitToFleeFrom)
	{
		if (this.position() !== undefined && unitToFleeFrom.position() !== undefined)
		{
			const FleeDirection = [this.position()[0] - unitToFleeFrom.position()[0],
				this.position()[1] - unitToFleeFrom.position()[1]];
			const dist = m.VectorDistance(unitToFleeFrom.position(), this.position());
			FleeDirection[0] = 40 * FleeDirection[0] / dist;
			FleeDirection[1] = 40 * FleeDirection[1] / dist;

			Engine.PostCommand(PlayerID, { "type": "walk", "entities": [this.id()], "x": this.position()[0] + FleeDirection[0], "z": this.position()[1] + FleeDirection[1], "queued": false, "pushFront": false });
		}
		return this;
	}

	gather(target, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "gather", "entities": [this.id()], "target": target.id(), "queued": queued, "pushFront": pushFront });
		return this;
	}

	repair(target, autocontinue = false, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "repair", "entities": [this.id()], "target": target.id(), "autocontinue": autocontinue, "queued": queued, "pushFront": pushFront });
		return this;
	}

	returnResources(target, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "returnresource", "entities": [this.id()], "target": target.id(), "queued": queued, "pushFront": pushFront });
		return this;
	}

	destroy()
	{
		Engine.PostCommand(PlayerID, { "type": "delete-entities", "entities": [this.id()] });
		return this;
	}

	barter(buyType, sellType, amount)
	{
		Engine.PostCommand(PlayerID, { "type": "barter", "sell": sellType, "buy": buyType, "amount": amount });
		return this;
	}

	tradeRoute(target, source)
	{
		Engine.PostCommand(PlayerID, { "type": "setup-trade-route", "entities": [this.id()], "target": target.id(), "source": source.id(), "route": undefined, "queued": false, "pushFront": false });
		return this;
	}

	setRallyPoint(target, command)
	{
		const data = { "command": command, "target": target.id() };
		Engine.PostCommand(PlayerID, { "type": "set-rallypoint", "structures": [this.id()], "x": target.position()[0], "z": target.position()[1], "data": data });
		return this;
	}

	unsetRallyPoint()
	{
		Engine.PostCommand(PlayerID, { "type": "unset-rallypoint", "structures": [this.id()] });
		return this;
	}

	train(civ, type, count, metadata, pushFront = false)
	{
		const trainable = this.trainableEntities(civ);
		if (!trainable)
		{
			error("Called train("+type+", "+count+") on non-training entity "+this);
			return this;
		}
		if (trainable.indexOf(type) == -1)
		{
			error("Called train("+type+", "+count+") on entity "+this+" which can't train that");
			return this;
		}

		Engine.PostCommand(PlayerID, {
			"type": "train",
			"entities": [this.id()],
			"template": type,
			"count": count,
			"metadata": metadata,
			"pushFront": pushFront
		});
		return this;
	}

	construct(template, x, z, angle, metadata)
	{
		// TODO: verify this unit can construct this, just for internal
		// sanity-checking and error reporting

		Engine.PostCommand(PlayerID, {
			"type": "construct",
			"entities": [this.id()],
			"template": template,
			"x": x,
			"z": z,
			"angle": angle,
			"autorepair": false,
			"autocontinue": false,
			"queued": false,
			"pushFront": false,
			"metadata": metadata	// can be undefined
		});
		return this;
	}

	research(template, pushFront = false)
	{
		Engine.PostCommand(PlayerID, {
			"type": "research",
			"entity": this.id(),
			"template": template,
			"pushFront": pushFront
		});
		return this;
	}

	stopProduction(id)
	{
		Engine.PostCommand(PlayerID, { "type": "stop-production", "entity": this.id(), "id": id });
		return this;
	}

	stopAllProduction(percentToStopAt)
	{
		const queue = this._entity.trainingQueue;
		if (!queue)
			return true;	// no queue, so technically we stopped all production.
		for (const item of queue)
			if (item.progress < percentToStopAt)
				Engine.PostCommand(PlayerID, { "type": "stop-production", "entity": this.id(), "id": item.id });
		return this;
	}

	guard(target, queued = false, pushFront = false)
	{
		Engine.PostCommand(PlayerID, { "type": "guard", "entities": [this.id()], "target": target.id(), "queued": queued, "pushFront": pushFront });
		return this;
	}

	removeGuard()
	{
		Engine.PostCommand(PlayerID, { "type": "remove-guard", "entities": [this.id()] });
		return this;
	}
}
