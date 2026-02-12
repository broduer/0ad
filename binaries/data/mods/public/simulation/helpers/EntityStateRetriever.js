/**
 * Helper class for the GuiInterface for retrieving an information about an entity or a template.
 * It assumes a certain level of caching on the receiving side (the GUI) and leverages it by only computing
 * the absolutely necessary ("unpredictable") parts of the desired data and leaves it up to the receiver to
 * put the whole data together by combining it with what has been returned previously already.
 *
 * It works like this:
 * Certain parts (the template data) of an entity state are "predictable" and can therefore be reused between
 * entities with the same template. What one has to account for, however, is that values of the template data
 * can be modified. This happens on two different levels:
 * - Firstly, player-global modifications, which apply to all entities owned by that player. This includes stuff
 *  like bonuses from researched techs, civs bonuses, team bonuses.
 * - Secondly, entity-local modifications, which apply to individual entities. This includes buffs or debuffs
 * 	from status effects or auras (of other entities).
 *
 * To take advantage of this, we separate the whole entity state into three parts:
 * 1. The template data of the entity's template, which only has the player-global modifications applied. It is
 *  the same for all entities with the same template and owner.
 * 2. The modified template data of the entity, which can contain a subset of the template data's values,
 *  overwriting them. It has both the player-global and entity-local modifications applied, but only contains the
 *  values that actually differ from its template data. It is therefore the same for all entities with the same
 *  owner, template, and modifications ID.
 * 3. the dynamic state, which differs from entity to entity and has to always be computed from scratch. It should be kept
 *   as small as possible.
 */
class EntityStateRetriever
{
	_computedTemplateData = {};

	/**
	 * Compute basic information about a given template accounting for all modifications registered to a given player.
	 * The returned data is (of course) consistent. So to avoid unnecessary work, this should never be called twice for the
	 * same player and template, unless the player's modifications changed in the meantime. It is the caller's responsibility
	 * to cache and reuse the results.
	 */
	getTemplateData(player, templateName)
	{
		if (this._hasComputedTemplateData(player, templateName))
			warn("GetTemplateData called multiple times for the template '" + templateName + "'and player " + player + ". The return value should have been cached in the GUI.");

		this._addComputedTemplateData(player, templateName);

		const template = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager)?.GetTemplate(templateName);
		const civ = QueryPlayerIDInterface(player, IID_Identity).GetCiv();
		return template && g_TemplateHelper.computeDataFromPlayer(template, AuraTemplates.GetAll(), Resources, player, civ);
	}

	/**
	 * Compute basic information about a given entity. This includes the template data, the modified template data, and
	 * the dynamic state, but only what hasn't been computed since the last time that the player modifications changed.
	 * The caller has to cache and reuse the data where possible. See the class description.
	 */
	getEntityState(player, ent)
	{
		if (!ent || ent == INVALID_ENTITY)
			return null;

		const cmpTemplateManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager);
		if (!cmpTemplateManager)
			return null;

		const templateName = cmpTemplateManager.GetCurrentTemplateName(ent);
		const template = templateName && cmpTemplateManager.GetTemplate(templateName);
		const owner = Engine.QueryInterface(ent, IID_Ownership)?.GetOwner();
		// All entities must have a template and an owner; if not then it's a nonexistent entity id.
		if (!template || owner === undefined)
			return null;

		const ret = {
			"dynamicState": this._computeDynamicState(owner, player, ent, templateName)
		};

		const civ = QueryPlayerIDInterface(owner, IID_Identity).GetCiv();
		if (!this._hasComputedTemplateData(owner, templateName))
		{
			ret.templateData = g_TemplateHelper.computeDataFromPlayer(template, AuraTemplates.GetAll(), Resources, owner, civ);
			this._addComputedTemplateData(owner, templateName);
		}

		const info = Engine.QueryInterface(SYSTEM_ENTITY, IID_ModifiersManager)?.GetModifiersInfo(ent);
		// A falsy modifications ID means the entity doesn't have any modifications applied locally, just the ones from its owner.
		if (info.modificationsID)
		{
			ret.modificationsID = info.modificationsID;
			if (!this._hasComputedTemplateData(owner, templateName, info.modificationsID))
			{
				ret.modifiedTemplateData = g_TemplateHelper.computeDataFromEntity(template, AuraTemplates.GetAll(), Resources, ent, civ, info.modifiedComponents);
				this._addComputedTemplateData(owner, templateName, info.modificationsID);
			}
		}

		return ret;
	}

	onPlayerModificationsChanged(player)
	{
		this._computedTemplateData[player]?.clear();
	}

	reset()
	{
		for (const player in this._computedTemplateData)
			this._computedTemplateData[player].clear();
	}

	_addComputedTemplateData(player, templateName, modificationsID = "")
	{
		let playerTable = this._computedTemplateData[player];
		if (!playerTable)
		{
			playerTable = new Set();
			this._computedTemplateData[player] = playerTable;
		}

		playerTable.add(`${templateName} ${modificationsID}`);
	}

	_hasComputedTemplateData(player, templateName, modificationsID = "")
	{
		return this._computedTemplateData[player]?.has(`${templateName} ${modificationsID}`);
	}

	/**
	 * Get an entity's dynamic state, which is unpredictable and differs from entity to entity.
	 * The information is pulled from its components.
	 * The structure of each component's data has to be coordinated with the one in TemplateHelper, so that they
	 * can later be merged into one.
	 * @param {number} owner - The owner of the entity.
	 * @param {number} player - The player, for whom to calculate the state for.
	 * @param {number} ent - ID of the entity.
	 * @param {string} templateName - The entity's template.
	 */
	_computeDynamicState(owner, player, ent, templateName)
	{
		const ret = {
			"id": ent,
			// TODO: Should maybe be renamed to owner for clarity.
			"player": owner,
			"templateName": templateName
		};

		const cmpAttack = Engine.QueryInterface(ent, IID_Attack);
		const cmpPosition = Engine.QueryInterface(ent, IID_Position);
		const cmpRangeManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_RangeManager);
		const cmpUnitAI = Engine.QueryInterface(ent, IID_UnitAI);
		if (cmpPosition?.IsInWorld() && cmpAttack?.GetAttackTypes().includes("Ranged"))
		{
			ret.attack = {
				"Ranged": {
					// For units, take the range in front of it, no spread, so angle = 0,
					// else, take the average elevation around it: angle = 2 * pi.
					"elevationAdaptedRange": cmpRangeManager.GetElevationAdaptedRange(
						cmpPosition.GetPosition(), cmpPosition.GetRotation(), cmpAttack.GetRange("Ranged").max,
						cmpAttack.GetAttackYOrigin("Ranged"), cmpUnitAI ? 0 : 2 * Math.PI
					)
				}
			};
		}

		const cmpBuildingAI = Engine.QueryInterface(ent, IID_BuildingAI);
		if (cmpBuildingAI)
			ret.buildingAI = {
				"arrowCount": cmpBuildingAI.GetArrowCount()
			};

		const cmpCapturable = QueryMiragedInterface(ent, IID_Capturable);
		if (cmpCapturable)
			ret.capturePoints = cmpCapturable.GetCapturePoints();

		const cmpIdentity = Engine.QueryInterface(ent, IID_Identity);
		ret.controllable = !cmpIdentity || cmpIdentity.IsControllable();

		const cmpFormation = Engine.QueryInterface(ent, IID_Formation);
		if (cmpFormation)
			ret.formation = {
				"members": cmpFormation.GetMembers()
			};

		const cmpFoundation = QueryMiragedInterface(ent, IID_Foundation);
		if (cmpFoundation)
			ret.foundation = {
				"numBuilders": cmpFoundation.GetNumBuilders(),
				"buildTime": cmpFoundation.GetBuildTime()
			};

		const cmpGarrisonable = Engine.QueryInterface(ent, IID_Garrisonable);
		if (cmpGarrisonable)
			ret.garrisonable = {
				"holder": cmpGarrisonable.HolderID()
			};

		const cmpGarrisonHolder = Engine.QueryInterface(ent, IID_GarrisonHolder);
		if (cmpGarrisonHolder)
			ret.garrisonHolder = {
				"entities": cmpGarrisonHolder.GetEntities(),
				"occupiedSlots": cmpGarrisonHolder.OccupiedSlots()
			};

		const cmpGate = Engine.QueryInterface(ent, IID_Gate);
		if (cmpGate)
			ret.gate = {
				"locked": cmpGate.IsLocked()
			};

		const cmpGuard = Engine.QueryInterface(ent, IID_Guard);
		if (cmpGuard)
			ret.guard = {
				"entities": cmpGuard.GetEntities()
			};

		const cmpHealth = QueryMiragedInterface(ent, IID_Health);
		if (cmpHealth)
		{
			ret.hitpoints = cmpHealth.GetHitpoints();
			ret.needsRepair = cmpHealth.IsRepairable() && cmpHealth.IsInjured();
			ret.needsHeal = !cmpHealth.IsUnhealable();
		}

		if (cmpPosition)
		{
			if (cmpPosition.IsInWorld())
				ret.position = cmpPosition.GetPosition();
			if (cmpPosition.GetTurretParent() != INVALID_ENTITY)
				ret.turretParent = cmpPosition.GetTurretParent();
		}

		const cmpPack = Engine.QueryInterface(ent, IID_Pack);
		if (cmpPack)
			ret.pack = {
				"packed": cmpPack.IsPacked(),
				"progress": cmpPack.GetProgress()
			};

		const cmpProductionQueue = Engine.QueryInterface(ent, IID_ProductionQueue);
		if (cmpProductionQueue)
			ret.production = {
				"queue": cmpProductionQueue.GetQueue(),
				"autoqueue": cmpProductionQueue.IsAutoQueueing()
			};

		const cmpPromotion = Engine.QueryInterface(ent, IID_Promotion);
		if (cmpPromotion)
			ret.promotion = {
				"curr": cmpPromotion.GetCurrentXp(),
			};

		const cmpRallyPoint = Engine.QueryInterface(ent, IID_RallyPoint);
		if (cmpRallyPoint)
			ret.rallyPoint = { "position": cmpRallyPoint.GetPositions()[0] }; // undefined or {x,z} object

		const cmpRepairable = QueryMiragedInterface(ent, IID_Repairable);
		if (cmpRepairable)
			ret.repairable = {
				"numBuilders": cmpRepairable.GetNumBuilders(),
				"buildTime": cmpRepairable.GetBuildTime()
			};

		const cmpResourceDropsite = Engine.QueryInterface(ent, IID_ResourceDropsite);
		if (cmpResourceDropsite)
			ret.resourceDropsite = {
				"shared": cmpResourceDropsite.IsShared()
			};

		const cmpResearcher = Engine.QueryInterface(ent, IID_Researcher);
		if (cmpResearcher)
			ret.researcher = {
				"technologies": cmpResearcher.GetTechnologiesList()
			};

		const cmpResourceGatherer = Engine.QueryInterface(ent, IID_ResourceGatherer);
		if (cmpResourceGatherer)
			ret.resourceCarrying = cmpResourceGatherer.GetCarryingStatus();

		const cmpResourceSupply = QueryMiragedInterface(ent, IID_ResourceSupply);
		if (cmpResourceSupply)
			ret.resourceSupply = {
				"amount": cmpResourceSupply.GetCurrentAmount(),
				"numGatherers": cmpResourceSupply.GetNumGatherers()
			};

		const cmpStatusEffects = Engine.QueryInterface(ent, IID_StatusEffectsReceiver);
		if (cmpStatusEffects)
			ret.statusEffects = cmpStatusEffects.GetActiveStatuses();

		const cmpTrader = Engine.QueryInterface(ent, IID_Trader);
		if (cmpTrader)
			ret.trader = {
				"goods": cmpTrader.GetGoods()
			};

		const cmpTrainer = Engine.QueryInterface(ent, IID_Trainer);
		if (cmpTrainer)
			ret.trainer = {
				// TODO: This is technically not "dynamic", since it only depends on the template and modifications,
				// so it should be made part of the template data instead in some way without causing too much code
				// duplication.
				"entities": cmpTrainer.GetEntitiesList()
			};

		const cmpTurretable = Engine.QueryInterface(ent, IID_Turretable);
		if (cmpTurretable)
			ret.turretable = {
				"ejectable": cmpTurretable.IsEjectable(),
				"holder": cmpTurretable.HolderID()
			};

		const cmpTurretHolder = Engine.QueryInterface(ent, IID_TurretHolder);
		if (cmpTurretHolder)
			ret.turretHolder = {
				"turretPoints": cmpTurretHolder.GetTurretPoints()
			};


		if (cmpUnitAI)
			ret.unitAI = {
				"state": cmpUnitAI.GetCurrentState(),
				"orders": cmpUnitAI.GetOrders(),
				"hasWorkOrders": cmpUnitAI.HasWorkOrders(),
				"isGuarding": cmpUnitAI.IsGuardOf(),
				"selectableStances": cmpUnitAI.GetSelectableStances(),
				"isIdle": cmpUnitAI.IsIdle(),
				"formation": cmpUnitAI.GetFormationController()
			};

		const cmpUpgrade = Engine.QueryInterface(ent, IID_Upgrade);
		if (cmpUpgrade)
			ret.upgrade = {
				"progress": cmpUpgrade.GetProgress(),
				"template": cmpUpgrade.GetUpgradingTo(),
				"isUpgrading": cmpUpgrade.IsUpgrading()
			};

		if (cmpRangeManager)
			ret.visibility = cmpRangeManager.GetLosVisibility(ent, player);


		// Because mirage entities mirage other entities' components, some values can't be statically read from their
		// template. They therefore are "missing" from their template data, so we retrieve them from the components
		// directly here and add them to the dynamic state instead, so that they're still present in the entire final
		// entity state (the GUI expects them).
		// This is somewhat hacky and it would be good to achieve this in another way.
		const cmpMirage = Engine.QueryInterface(ent, IID_Mirage);
		if (cmpMirage)
		{
			if (cmpCapturable)
				ret.maxCapturePoints = cmpCapturable.GetMaxCapturePoints();

			if (cmpHealth)
				ret.maxHitpoints = cmpHealth.GetMaxHitpoints();

			if (cmpResourceSupply)
			{
				ret.resourceSupply.isInfinite = cmpResourceSupply.IsInfinite();
				ret.resourceSupply.max = cmpResourceSupply.GetMaxAmount();
				ret.resourceSupply.type = cmpResourceSupply.GetType();
				ret.resourceSupply.killBeforeGather = cmpResourceSupply.GetKillBeforeGather();
				ret.resourceSupply.maxGatherers = cmpResourceSupply.GetMaxGatherers();
			}
		}

		return ret;
	}
}

Engine.RegisterGlobal("EntityStateRetriever", EntityStateRetriever);
