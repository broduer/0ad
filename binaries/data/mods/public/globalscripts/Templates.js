/**
 * Loads history and gameplay data of all civs.
 *
 * @param selectableOnly {boolean} - Only load civs that can be selected
 *        in the gamesetup. Scenario maps might set non-selectable civs.
 */
function loadCivFiles(selectableOnly)
{
	const propertyNames = [
		"Code", "Culture", "Music", "CivBonuses", "StartEntities",
		"AINames", "SkirmishReplacements", "SelectableInGameSetup"];

	const civData = {};

	for (const filename of Engine.ListDirectoryFiles("simulation/data/civs/", "*.json", false))
	{
		const data = Engine.ReadJSONFile(filename);

		for (const prop of propertyNames)
			if (data[prop] === undefined)
				throw new Error(filename + " doesn't contain " + prop);

		if (selectableOnly && !data.SelectableInGameSetup)
			continue;

		const template = Engine.GetTemplate("special/players/" + data.Code);
		data.Name = template.Identity.GenericName;
		data.Emblem = "session/portraits/" + template.Identity.Icon;
		data.History = template.Identity.History;

		civData[data.Code] = data;
	}

	return civData;
}

/**
 * @return {string[]} - All the classes for this identity template.
 */
function GetIdentityClasses(template)
{
	let classString = "";

	if (template.Classes && template.Classes._string)
		classString += " " + template.Classes._string;

	if (template.VisibleClasses && template.VisibleClasses._string)
		classString += " " + template.VisibleClasses._string;

	if (template.Rank)
		classString += " " + template.Rank;

	return classString.length > 1 ? classString.substring(1).split(" ") : [];
}

/**
 * Gets an array with all classes for this identity template
 * that should be shown in the GUI
 */
function GetVisibleIdentityClasses(template)
{
	return template.VisibleClasses && template.VisibleClasses._string ? template.VisibleClasses._string.split(" ") : [];
}

/**
 * Check if a given list of classes matches another list of classes.
 * Useful f.e. for checking identity classes.
 *
 * @param classes - List of the classes to check against.
 * @param match - Either a string in the form
 *     "Class1 Class2+Class3"
 * where spaces are handled as OR and '+'-signs as AND,
 * and ! is handled as NOT, thus Class1+!Class2 = Class1 AND NOT Class2.
 * Or a list in the form
 *     [["Class1"], ["Class2", "Class3"]]
 * where the outer list is combined as OR, and the inner lists are AND-ed.
 * Or a hybrid format containing a list of strings, where the list is
 * combined as OR, and the strings are split by space and '+' and AND-ed.
 *
 * @return undefined if there are no classes or no match object
 * true if the the logical combination in the match object matches the classes
 * false otherwise.
 */
function MatchesClassList(classes, match)
{
	if (!match || !classes)
		return undefined;
	// Transform the string to an array
	if (typeof match === "string")
		match = match.split(/\s+/);

	for (let sublist of match)
	{
		// If the elements are still strings, split them by space or by '+'
		if (typeof sublist === "string")
			sublist = sublist.split(/[+\s]+/);
		if (sublist.every(c => (c[0] === "!" && classes.indexOf(c.substr(1)) === -1) ||
		                       (c[0] !== "!" && classes.indexOf(c) !== -1)))
			return true;
	}

	return false;
}

/**
 * Get basic information about a technology template.
 * @param {Object} template - A valid template as obtained by loading the tech JSON file.
 * @param {string} civ - Civilization for which the tech requirements should be calculated.
 */
function GetTechnologyBasicDataHelper(template, civ)
{
	return {
		"name": {
			"generic": template.genericName
		},
		"icon": template.icon ? "technologies/" + template.icon : undefined,
		"description": template.description,
		"reqs": DeriveTechnologyRequirements(template, civ),
		"modifications": template.modifications,
		"affects": template.affects,
		"replaces": template.replaces
	};
}

/**
 * Get information about a technology template.
 * @param {Object} template - A valid template as obtained by loading the tech JSON file.
 * @param {string} civ - Civilization for which the specific name and tech requirements should be returned.
 * @param {Object} resources - An instance of the Resources class.
 */
function GetTechnologyDataHelper(template, civ, resources)
{
	const ret = GetTechnologyBasicDataHelper(template, civ);

	if (template.specificName)
		ret.name.specific = template.specificName[civ] || template.specificName.generic;

	ret.cost = { "time": template.researchTime ? +template.researchTime : 0 };
	for (const type of resources.GetCodes())
		ret.cost[type] = +(template.cost && template.cost[type] || 0);

	ret.tooltip = template.tooltip;
	ret.requirementsTooltip = template.requirementsTooltip || "";
	if (template.placeBelow)
		ret.placeBelow = template.placeBelow;

	return ret;
}

/**
 * Get information about an aura template.
 * @param {object} template - A valid template as obtained by loading the aura JSON file.
 */
function GetAuraDataHelper(template)
{
	return {
		"name": {
			"generic": template.auraName,
		},
		"description": template.auraDescription || null,
		"modifications": template.modifications,
		"radius": template.radius || null,
	};
}

function calculateCarriedResources(carriedResources, tradingGoods)
{
	var resources = {};

	if (carriedResources)
		for (const resource of carriedResources)
			resources[resource.type] = (resources[resource.type] || 0) + resource.amount;

	if (tradingGoods && tradingGoods.amount)
		resources[tradingGoods.type] =
			(resources[tradingGoods.type] || 0) +
			(tradingGoods.amount.traderGain || 0) +
			(tradingGoods.amount.market1Gain || 0) +
			(tradingGoods.amount.market2Gain || 0);

	return resources;
}

/**
 * Remove filter prefix (mirage, corpse, etc) from template name.
 *
 * ie. filter|dir/to/template -> dir/to/template
 */
function removeFiltersFromTemplateName(templateName)
{
	return templateName.split("|").pop();
}

/**
 * Helper providing several options to compute an already loaded template's "data", i.e. interesting values with
 * dynamic modifications (from e.g. auras, techs, or civ bonuses) applied.
 */
class TemplateHelper
{
	/**
	 * Stores some temporary data from the current query to avoid having to pass it around all the time
	 * and to keep the computations clearer and cleaner.
	 */
	_context = Object.seal({
		"template": {},
		"player": null, // null or a player id
		"civ": "",
		"entity": null, // null or an entity id
		"auraTemplates": {},
		"resources": {},
		"customModifiers": null, // null or an object defining modifications
		"applyValueModifications": () => 0
	});

	/**
	 * Pull basic information from a template without applying any modifiers whatsoever.
	 *
	 * @param {Object} template - A valid template as returned by the template loader.
	 * @param {Object} auraTemplates - In the form of { key: { "auraName": "", "auraDescription": "" } }.
	 * @param {Object} resources - An instance of the Resources class.
	 * @param {string} civ - The civ of which the template is part of. Does not have to match the template's identity's own civ.
	 * @param {string[]} [components] - An array of components to process, if undefined all components are processed.
	 */
	getBasicData(template, auraTemplates, resources, civ, components)
	{
		return this.computeDataFromModifiers(template, auraTemplates, resources, {}, civ, components);
	}

	/**
	 * Pull basic information from a template and apply a given set of modifiers to it.
	 *
	 * @param {Object} template - A valid template as returned by the template loader.
	 * @param {Object} auraTemplates - In the form of { key: { "auraName": "", "auraDescription": "" } }.
	 * @param {Object} resources - An instance of the Resources class.
	 * @param {Object} modifiers - Modifications to apply to the template, e.g. from auto-researched techs or civ bonuses.
	 * @param {string} civ - The civ of which the template is part of. Does not have to match the template's identity's own civ.
	 * @param {string[]} [components] - An array of components to process, if undefined all components are processed.
	 */
	computeDataFromModifiers(template, auraTemplates, resources, modifiers, civ, components)
	{
		this._context.template = template;
		this._context.player = null;
		this._context.civ = civ;
		this._context.entity = null;
		this._context.auraTemplates = auraTemplates;
		this._context.resources = resources;
		this._context.customModifiers = modifiers;
		this._context.applyValueModifications = this._applyCustomModifications.bind(this);

		return this._computeModifiedTemplateData(template, components);
	}

	/**
	 * Pull basic information from a template and to it apply all modifiers registered (in the simulation) to a given player.
	 * Must only be called in simulation context.
	 *
	 * @param {Object} template - A valid template as returned by the template loader.
	 * @param {Object} auraTemplates - In the form of { key: { "auraName": "", "auraDescription": "" } }.
	 * @param {Object} resources - An instance of the Resources class.
	 * @param {number} player - ID of the target player.
	 * @param {string} playerCiv - Civ of the target player.
	 * @param {string[]} [components] - An array of components to process, if undefined all components are processed.
	 */
	computeDataFromPlayer(template, auraTemplates, resources, player, playerCiv, components)
	{
		this._context.template = template;
		this._context.player = player;
		this._context.civ = playerCiv;
		this._context.entity = null;
		this._context.auraTemplates = auraTemplates;
		this._context.resources = resources;
		this._context.customModifiers = null;
		this._context.applyValueModifications = this._applyPlayerModifications.bind(this);

		return this._computeModifiedTemplateData(template, components);
	}

	/**
	 * Pull basic information from a template and to it apply all modifiers registered (in the simulation) to a given entity or
	 * its owning player.
	 * Must only be called in simulation context.
	 *
	 * @param {Object} template - A valid template as returned by the template loader.
	 * @param {Object} auraTemplates - In the form of { key: { "auraName": "", "auraDescription": "" } }.
	 * @param {Object} resources - An instance of the Resources class.
	 * @param {number} entity - ID of the target entity.
	 * @param {string} ownerCiv - Civ of the target entity's owner.
	 * @param {undefined|string[]} components - An array of components to process, if undefined all components are processed.
	 */
	computeDataFromEntity(template, auraTemplates, resources, entity, ownerCiv, components)
	{
		this._context.template = template;
		this._context.player = null;
		this._context.civ = ownerCiv;
		this._context.entity = entity;
		this._context.auraTemplates = auraTemplates;
		this._context.resources = resources;
		this._context.customModifiers = null;
		this._context.applyValueModifications = this._applyEntityModifications.bind(this);

		return this._computeModifiedTemplateData(template, components);
	}

	/**
	 * Apply the manually-passed ("custom") modifiers to a given value.
	 */
	_applyCustomModifications(currentValue, modKey)
	{
		if (this._context.modifiers?.[modKey])
			return GetTechModifiedProperty(
				this._context.modifiers[modKey], GetIdentityClasses(this._context.template.Identity), currentValue
			);
		return currentValue;
	}

	/**
	 * Apply the modifiers of the current player to a given value.
	 */
	_applyPlayerModifications(currentValue, modKey)
	{
		return ApplyValueModificationsToTemplate(modKey, currentValue, this._context.player, this._context.template);
	}

	/**
	 * Apply all modifiers of the current entity and of its owner to a given value.
	 */
	_applyEntityModifications(currentValue, modKey)
	{
		return ApplyValueModificationsToEntity(modKey, currentValue, this._context.entity);
	}

	/**
	 * Read a given value from the current template, and apply the current modifications to it.
	 * Only call this for values that can theoretically be modified (i.e. the simulation supports it), like attack damge or max HP.
	 * Static, unmodifiable values (like turret points or footprint size) are better read from the template directly.
	 */
	_getModifiedValue(valuePath, modKey, isNumeric = true, defaultValue = 0)
	{
		let currentValue = this._context.template;
		for (const property of valuePath.split("/"))
			currentValue = currentValue[property] || defaultValue;

		return isNumeric ?
		// Using .toFixed() to get around spidermonkey's treatment of numbers (3 * 1.1 = 3.3000000000000003 for instance).
			+this._context.applyValueModifications(+currentValue, modKey || valuePath).toFixed(8) :
			this._context.applyValueModifications(currentValue, modKey || valuePath);
	}

	/**
	 * Pull the desired information from a template while accounting for modifications of the current context.
	 */
	_computeModifiedTemplateData(template, components)
	{
		const ret = {};
		for (const component of (components || Object.keys(template)))
		{
			const handler = this._componentHandlers[component];
			if (!handler)
				// There doesn't need to be a handler for each component, so just skip it silently.
				continue;

			handler(template[component], ret);
		}

		return ret;
	}

	/**
	 * Simple helper for the component handlers.
	 */
	_getAttackEffects(template, path)
	{
		const ret = {};
		if (template.Capture)
			ret.Capture = this._getModifiedValue(path + "/Capture");

		if (template.Damage)
		{
			ret.Damage = {};
			for (const damageType in template.Damage)
				ret.Damage[damageType] = this._getModifiedValue(path + "/Damage/" + damageType);
		}

		if (template.ApplyStatus)
			ret.ApplyStatus = template.ApplyStatus;

		return ret;
	}

	/**
	 * The methods performing the core logic, pulling and processing the values of the template's individual
	 * components.
	 * The structure of each component's data has to be coordinated with the one in EntityStateRetriever, so that they
	 * can be merged into one.
	 */
	_componentHandlers = {
		"AlertRaiser": (alertRaiser, ret) =>
		{
			ret.alertRaiser = { "classes": alertRaiser.List._string };
		},
		"Attack": (attack, ret) =>
		{
			ret.attack = {};

			for (const type in attack)
			{
				const attackType = attack[type];
				const data = this._getAttackEffects(attackType, "Attack/" + type);

				const getAttackStat = stat =>
					this._getModifiedValue("Attack/" + type + "/" + stat);

				data.attackName = {
					"name": attackType.AttackName._string || attackType.AttackName
				};
				const context = attackType.AttackName["@context"];
				if (context)
					data.attackName.context = context;

				data.minRange = getAttackStat("MinRange");
				data.maxRange = getAttackStat("MaxRange");
				data.yOrigin = getAttackStat("Origin/Y");

				data.elevationAdaptedRange =
					Math.sqrt(data.maxRange *
						(2 * data.yOrigin + data.maxRange));

				data.repeatTime = getAttackStat("RepeatTime");

				if (attackType.Projectile)
					data.projectileCount = +attackType.Projectile.Count || 1;

				if (attackType.Projectile)
					data.friendlyFire = attackType.Projectile.FriendlyFire === "true";

				if (attackType.Splash)
				{
					data.splash = this._getAttackEffects(
						attackType.Splash,
						"Attack/" + type + "/Splash"
					);
					data.splash.friendlyFire = attackType.Splash.FriendlyFire !== "false";
					data.splash.shape = attackType.Splash.Shape;
				}

				ret.attack[type] = data;
			}
		},
		"Auras": (auras, ret) =>
		{
			ret.auras = {};

			if (this._context.auraTemplates)
				for (const auraID of auras._string.split(/\s+/))
					ret.auras[auraID] =
						GetAuraDataHelper(this._context.auraTemplates[auraID]);
		},
		"BuildRestrictions": (buildRestrictions, ret) =>
		{
			ret.buildRestrictions = {
				"placementType": buildRestrictions.PlacementType,
				"territory": buildRestrictions.Territory,
				"category": buildRestrictions.Category
			};

			if (buildRestrictions.Distance)
			{
				ret.buildRestrictions.distance = {
					"fromClass": buildRestrictions.Distance.FromClass
				};

				if (buildRestrictions.Distance.MinDistance)
					ret.buildRestrictions.distance.min =
						this._getModifiedValue("BuildRestrictions/Distance/MinDistance");

				if (buildRestrictions.Distance.MaxDistance)
					ret.buildRestrictions.distance.max =
						this._getModifiedValue("BuildRestrictions/Distance/MaxDistance");
			}
		},
		"Builder": (builder, ret) =>
		{
			ret.builder = true;
		},
		"BuildingAI": (buildingAI, ret) =>
		{
			ret.buildingAI = {
				"defaultArrowCount": Math.round(this._getModifiedValue("BuildingAI/DefaultArrowCount")),
				"maxArrowCount": Math.round(this._getModifiedValue("BuildingAI/MaxArrowCount")),
				"garrisonArrowMultiplier": this._getModifiedValue("BuildingAI/GarrisonArrowMultiplier"),
				"garrisonArrowClasses": buildingAI.GarrisonArrowClasses?.split(/\s+/)
			};
		},
		"Capturable": (capturable, ret) =>
		{
			ret.maxCapturePoints = Math.round(this._getModifiedValue("Capturable/CapturePoints"));
		},
		"Cost": (cost, ret) =>
		{
			ret.cost = {};

			for (const resCode in cost.Resources)
				ret.cost[resCode] =
					this._getModifiedValue("Cost/Resources/" + resCode);

			if (cost.Population)
				ret.cost.population = this._getModifiedValue("Cost/Population");

			if (cost.BuildTime)
				ret.cost.time = this._getModifiedValue("Cost/BuildTime");
		},
		"DeathDamage": (deathDamage, ret) =>
		{
			ret.deathDamage = this._getAttackEffects(deathDamage, "DeathDamage");
			ret.deathDamage.friendlyFire = deathDamage.FriendlyFire != "false";
		},
		"Footprint": (footprint, ret) =>
		{
			ret.footprint = { "height": footprint.Height };

			if (footprint.Square)
				ret.footprint.square = {
					"width": +footprint.Square["@width"],
					"depth": +footprint.Square["@depth"]
				};
			else if (footprint.Circle)
				ret.footprint.circle = {
					"radius": +footprint.Circle["@radius"]
				};
			else
				warn("TemplateHelper: Unrecognized Footprint type");
		},
		"Garrisonable": (garrisonable, ret) =>
		{
			ret.garrisonable = {
				"size": this._getModifiedValue("Garrisonable/Size")
			};
		},
		"GarrisonHolder": (garrisonHolder, ret) =>
		{
			ret.garrisonHolder = {
				"allowedClasses": this._getModifiedValue("GarrisonHolder/List/_string", null, false, ""),
				"buffHeal": this._getModifiedValue("GarrisonHolder/BuffHeal"),
				"capacity": this._getModifiedValue("GarrisonHolder/Max")
			};
		},
		"Heal": (heal, ret) =>
		{
			ret.heal = {
				"health": this._getModifiedValue("Heal/Health"),
				"range": this._getModifiedValue("Heal/Range"),
				"interval": this._getModifiedValue("Heal/Interval"),
				"unhealableClasses": heal.UnhealableClasses._string || "",
				"healableClasses": heal.HealableClasses._string || ""
			};
		},
		"Health": (health, ret) =>
		{
			ret.maxHitpoints = Math.round(this._getModifiedValue("Health/Max"));
		},
		"Identity": (identity, ret) =>
		{
			const allClasses = GetIdentityClasses(identity);
			ret.selectionGroupName = identity.SelectionGroupName;
			ret.name = {
				"specific": (identity.SpecificName || identity.GenericName),
				"generic": identity.GenericName
			};
			ret.icon = identity.Icon;
			ret.tooltip = identity.Tooltip;
			ret.identityClasses = allClasses;
			ret.visibleIdentityClasses = GetVisibleIdentityClasses(identity);
			ret.nativeCiv = identity.Civ;
			ret.requirements = identity.Requirements;
			ret.rank = identity.Rank;
			ret.undeletable = identity.Undeletable;
			ret.enablesBartering = allClasses.includes("Barter") && !this._context.template.Foundation;

		},
		"Loot": (loot, ret) =>
		{
			ret.loot = {};

			for (const type in loot)
				ret.loot[type] =
					this._getModifiedValue("Loot/" + type);
		},
		"Market": (market, ret) =>
		{
			const tradeTypes = market.TradeType.split(/\s+/);

			ret.market = {
				"land": tradeTypes.includes("land"),
				"naval": tradeTypes.includes("naval")
			};
		},
		"Mirage": (mirage, ret) =>
		{
			ret.mirage = true;
		},
		"Obstruction": (obstruction, ret) =>
		{
			ret.obstruction = {
				"active": ("" + obstruction.Active === "true"),
				"blockMovement": ("" + obstruction.BlockMovement === "true"),
				"blockPathfinding": ("" + obstruction.BlockPathfinding === "true"),
				"blockFoundation": ("" + obstruction.BlockFoundation === "true"),
				"blockConstruction": ("" + obstruction.BlockConstruction === "true"),
				"disableBlockMovement": ("" + obstruction.DisableBlockMovement === "true"),
				"disableBlockPathfinding": ("" + obstruction.DisableBlockPathfinding === "true"),
				"shape": {}
			};

			if (obstruction.Static)
			{
				ret.obstruction.shape.type = "static";
				ret.obstruction.shape.width = +obstruction.Static["@width"];
				ret.obstruction.shape.depth = +obstruction.Static["@depth"];
			}
			else if (obstruction.Unit)
			{
				ret.obstruction.shape.type = "unit";
				ret.obstruction.shape.radius = +obstruction.Unit["@radius"];
			}
			else
			{
				ret.obstruction.shape.type = "cluster";
			}
		},
		"Pack": (pack, ret) =>
		{
			ret.pack = {
				"state": pack.State,
				"time": this._getModifiedValue("Pack/Time")
			};
		},
		"Population": (population, ret) =>
		{
			if (!population.Bonus)
				return;

			ret.population = {
				"bonus": this._getModifiedValue("Population/Bonus")
			};
		},
		"Promotion": (promotion, ret) =>
		{
			ret.promotion = {
				"req": this._getModifiedValue("Promotion/RequiredXp")
			};
		},
		"Researcher": (researcher, ret) =>
		{
			ret.researcher = {
				"techCostMultiplier": {}
			};

			for (const res of this._context.resources.GetCodes().concat(["time"]))
				ret.researcher.techCostMultiplier[res] =
					this._getModifiedValue("Researcher/TechCostMultiplier/" + res, null, true, 1);
		},
		"Resistance": (resistance, ret) =>
		{
			const form = this._context.template.Foundation ? "Foundation" : "Entity";
			const resistanceForm = resistance?.[form];
			if (!resistanceForm)
				return;

			ret.resistance = {};

			if (resistanceForm.Damage)
			{
				ret.resistance.Damage = {};
				for (const damageType in resistanceForm.Damage)
					ret.resistance.Damage[damageType] =
						this._getModifiedValue("Resistance/Entity/Damage/" + damageType);
			}

			if (resistanceForm.Capture)
				ret.resistance.Capture = this._getModifiedValue("Resistance/Entity/Capture");

			if (resistanceForm.ApplyStatus)
			{
				ret.resistance.ApplyStatus = {};
				for (const statusEffect in resistanceForm.ApplyStatus)
					ret.resistance.ApplyStatus[statusEffect] = {
						"blockChance": this._getModifiedValue("Resistance/Entity/ApplyStatus/" + statusEffect + "/BlockChance"),
						"duration": this._getModifiedValue("Resistance/Entity/ApplyStatus/" + statusEffect + "/Duration")
					};
			}
		},
		"ResourceDropsite": (resourceDropsite, ret) =>
		{
			ret.resourceDropsite = {
				"types": resourceDropsite.Types.split(" ")
			};
		},
		"ResourceGatherer": (resourceGatherer, ret) =>
		{
			ret.resourceGatherRates = {};

			const baseSpeed = this._getModifiedValue("ResourceGatherer/BaseSpeed");

			for (const type in resourceGatherer.Rates)
				ret.resourceGatherRates[type] =
					this._getModifiedValue("ResourceGatherer/Rates/" + type) * baseSpeed;
		},
		"ResourceSupply": (resourceSupply, ret) =>
		{
			const i = resourceSupply.Type.indexOf('.');
			ret.resourceSupply = {
				"isInfinite": !isFinite(+resourceSupply.Max),
				"max": this._getModifiedValue("ResourceSupply/Max"),
				"type": {
					"generic": resourceSupply.Type.slice(0, i),
					"specific": resourceSupply.Type.substring(i + 1)
				},
				"killBeforeGather": resourceSupply.KillBeforeGather === "true",
				"maxGatherers": +resourceSupply.MaxGatherers
			};
		},
		"ResourceTrickle": (resourceTrickle, ret) =>
		{
			ret.resourceTrickle = {
				"interval": +resourceTrickle.Interval,
				"rates": {}
			};

			for (const type in resourceTrickle.Rates)
				ret.resourceTrickle.rates[type] =
					this._getModifiedValue("ResourceTrickle/Rates/" + type);
		},
		"Trader": (trader, ret) =>
		{
			ret.trader = {
				"GainMultiplier": this._getModifiedValue("Trader/GainMultiplier")
			};
		},
		"Treasure": (treasure, ret) =>
		{
			ret.treasure = {
				"collectTime": this._getModifiedValue("Treasure/CollectTime"),
				"resources": {}
			};

			for (const resource in treasure.Resources)
				ret.treasure.resources[resource] =
					this._getModifiedValue("Treasure/Resources/" + resource);
		},
		"TreasureCollector": (treasureCollector, ret) =>
		{
			ret.treasureCollector = true;
		},
		"TurretHolder": (turretHolder, ret) =>
		{
			ret.turretHolder = {
				"numTurretPoints": Object.keys(turretHolder.TurretPoints).length
			};
		},
		"UnitAI": (unitAI, ret) =>
		{
			ret.unitAI = {
				"formations": unitAI.Formations?._string?.split(/\s+/) || []
			};
		},
		"UnitMotion": (unitMotion, ret) =>
		{
			const walkSpeed = this._getModifiedValue("UnitMotion/WalkSpeed");

			ret.unitMotion = {
				"walk": walkSpeed,
				"run": walkSpeed,
				"acceleration": this._getModifiedValue("UnitMotion/Acceleration")
			};

			if (unitMotion.RunMultiplier)
				ret.unitMotion.run *= this._getModifiedValue("UnitMotion/RunMultiplier");
		},
		"Upkeep": (upkeep, ret) =>
		{
			ret.upkeep = {
				"interval": +upkeep.Interval,
				"rates": {}
			};

			for (const type in upkeep.Rates)
				ret.upkeep.rates[type] =
					this._getModifiedValue("Upkeep/Rates/" + type);
		},
		"Upgrade": (upgrade, ret) =>
		{
			ret.upgrade = { "options": [] };

			for (const upgradeName in upgrade)
			{
				const upgr = upgrade[upgradeName];

				const cost = {};
				if (upgr.Cost)
					for (const res in upgr.Cost)
						cost[res] =
							this._getModifiedValue("Upgrade/" + upgradeName + "/Cost/" + res, "Upgrade/Cost/" + res);

				if (upgr.Time)
					cost.time =
						this._getModifiedValue("Upgrade/" + upgradeName + "/Time", "Upgrade/Time");

				ret.upgrade.options.push({
					"entity": upgr.Entity
						.replace(/\{civ\}/g, this._context.civ)
						.replace(/\{native\}/g, this._context.template.Identity.Civ),
					"tooltip": upgr.Tooltip,
					cost,
					"icon": upgr.Icon,
					"requirements": upgr.Requirements
				});
			}
		},
		"WallPiece": (wallPiece, ret) =>
		{
			ret.wallPiece = {
				"length": +wallPiece.Length,
				"angle": +(wallPiece.Orientation || 1) * Math.PI,
				"indent": +(wallPiece.Indent || 0),
				"bend": +(wallPiece.Bend || 0) * Math.PI
			};
		},
		"WallSet": (wallSet, ret) =>
		{
			ret.wallSet = {
				"templates": {
					"tower": wallSet.Templates.Tower,
					"gate": wallSet.Templates.Gate,
					"fort": wallSet.Templates.Fort || "structures/" + this._context.template.Identity.Civ + "/fortress",
					"long": wallSet.Templates.WallLong,
					"medium": wallSet.Templates.WallMedium,
					"short": wallSet.Templates.WallShort
				},
				"maxTowerOverlap": +wallSet.MaxTowerOverlap,
				"minTowerOverlap": +wallSet.MinTowerOverlap
			};

			if (wallSet.Templates.WallEnd)
				ret.wallSet.templates.end = wallSet.Templates.WallEnd;

			if (wallSet.Templates.WallCurves)
				ret.wallSet.templates.curves = wallSet.Templates.WallCurves.split(/\s+/);
		}
	};
}

var g_TemplateHelper = new TemplateHelper();
