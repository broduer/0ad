import { Entity } from "simulation/ai/common-api/entity.js";
import { EntityCollection } from "simulation/ai/common-api/entitycollection.js";
import { GameState } from "simulation/ai/common-api/gamestate.js";
import { InfoMap } from "simulation/ai/common-api/map-module.js";
import { Accessibility, TerrainAnalysis } from "simulation/ai/common-api/terrain-analysis.js";

/** Shared script handling templates and basic terrain analysis */
export class SharedScript
{
	constructor(settings)
	{
		if (!settings)
			return;

		this._players = Object.keys(settings.players).map(key => settings.players[key]); // TODO SM55 Object.values(settings.players)
		this._templates = settings.templates;

		this._entityMetadata = {};
		for (const player of this._players)
			this._entityMetadata[player] = {};

		// array of entity collections
		this._entityCollections = new Map();
		this._entitiesModifications = new Map();	// entities modifications
		this._templatesModifications = {};	// template modifications
		// each name is a reference to the actual one.
		this._entityCollectionsName = new Map();
		this._entityCollectionsByDynProp = {};
		this._entityCollectionsUID = 0;
	}

	/** Return a simple object (using no classes etc) that will be serialized into saved games */
	Serialize()
	{
		return {
			"players": this._players,
			"templatesModifications": this._templatesModifications,
			"entitiesModifications": this._entitiesModifications,
			"metadata": this._entityMetadata
		};
	}

	/**
	 * Called after the constructor when loading a saved game, with 'data' being
	 * whatever Serialize() returned
	 */
	Deserialize(data)
	{
		this._players = data.players;
		this._templatesModifications = data.templatesModifications;
		this._entitiesModifications = data.entitiesModifications;
		this._entityMetadata = data.metadata;

		this.isDeserialized = true;
	}

	GetTemplate(name)
	{
		if (this._templates[name] === undefined)
			this._templates[name] = Engine.GetTemplate(name) || null;

		return this._templates[name];
	}

	/**
	 * Initialize the shared component.
	 * We need to know the initial state of the game for this, as we will use it.
	 * This is called right at the end of the map generation.
	 */
	init(state, deserialization)
	{
		if (!deserialization)
			this._entitiesModifications = new Map();

		this.ApplyTemplatesDelta(state);

		this.passabilityClasses = state.passabilityClasses;
		this.playersData = state.players;
		this.timeElapsed = state.timeElapsed;
		this.circularMap = state.circularMap;
		this.mapSize = state.mapSize;
		this.victoryConditions = new Set(state.victoryConditions);
		this.alliedVictory = state.alliedVictory;
		this.ceasefireActive = state.ceasefireActive;
		this.ceasefireTimeRemaining = state.ceasefireTimeRemaining / 1000;

		this.passabilityMap = state.passabilityMap;
		if (this.mapSize % this.passabilityMap.width !== 0)
			error("AI shared component inconsistent sizes: map=" + this.mapSize + " while passability=" + this.passabilityMap.width);
		this.passabilityMap.cellSize = this.mapSize / this.passabilityMap.width;
		this.territoryMap = state.territoryMap;
		if (this.mapSize % this.territoryMap.width !== 0)
			error("AI shared component inconsistent sizes: map=" + this.mapSize + " while territory=" + this.territoryMap.width);
		this.territoryMap.cellSize = this.mapSize / this.territoryMap.width;

		/*
		let landPassMap = new Uint8Array(this.passabilityMap.data.length);
		let waterPassMap = new Uint8Array(this.passabilityMap.data.length);
		let obstructionMaskLand = this.passabilityClasses["default-terrain-only"];
		let obstructionMaskWater = this.passabilityClasses["ship-terrain-only"];
		for (let i = 0; i < this.passabilityMap.data.length; ++i)
		{
			landPassMap[i] = (this.passabilityMap.data[i] & obstructionMaskLand) ? 0 : 255;
			waterPassMap[i] = (this.passabilityMap.data[i] & obstructionMaskWater) ? 0 : 255;
		}
		Engine.DumpImage("LandPassMap.png", landPassMap, this.passabilityMap.width, this.passabilityMap.height, 255);
		Engine.DumpImage("WaterPassMap.png", waterPassMap, this.passabilityMap.width, this.passabilityMap.height, 255);
	*/

		this._entities = new Map();
		if (state.entities)
			for (const id in state.entities)
				this._entities.set(+id, new Entity(this, state.entities[id]));
		// entity collection updated on create/destroy event.
		this.entities = new EntityCollection(this, this._entities);

		// create the terrain analyzer
		this.terrainAnalyzer = new TerrainAnalysis(this, state);
		this.accessibility = new Accessibility(state, this.terrainAnalyzer);

		// Resource types: ignore = not used for resource maps
		//                 abundant = abundant resource with small amount each
		//                 sparse = sparse resource, but huge amount each
		// The following maps are defined in TerrainAnalysis.js and are used for some building placement (cc, dropsites)
		// They are updated by checking for create and destroy events for all resources
		this.normalizationFactor = { "abundant": 50, "sparse": 90 };
		this.influenceRadius = { "abundant": 36, "sparse": 48 };
		this.ccInfluenceRadius = { "abundant": 60, "sparse": 120 };

		// The influence of each resource on the resource maps, by entity id, to remove it when the resource is destroyed.
		this.resources = new Map();
		this.resourceMaps = {};   // Contains maps showing the density of resources
		this.ccResourceMaps = {}; // Contains maps showing the density of resources, optimized for CC placement.
		this.createResourceMaps();

		this.gameState = {};
		for (const player of this._players)
		{
			this.gameState[player] = new GameState();
			this.gameState[player].init(this, state, player);
		}
	}

	/**
	 * General update of the shared script, before each AI's update
	 * applies entity deltas, and each gamestate.
	 */
	onUpdate(state)
	{
		if (this.isDeserialized)
		{
			this.init(state, true);
			this.isDeserialized = false;
		}

		// deals with updating based on create and destroy messages.
		this.ApplyEntitiesDelta(state);
		this.ApplyTemplatesDelta(state);

		Engine.ProfileStart("onUpdate");

		// those are dynamic and need to be reset as the "state" object moves in memory.
		this.events = state.events;
		this.passabilityClasses = state.passabilityClasses;
		this.playersData = state.players;
		this.timeElapsed = state.timeElapsed;
		this.barterPrices = state.barterPrices;
		this.ceasefireActive = state.ceasefireActive;
		this.ceasefireTimeRemaining = state.ceasefireTimeRemaining / 1000;

		this.passabilityMap = state.passabilityMap;
		this.passabilityMap.cellSize = this.mapSize / this.passabilityMap.width;
		this.territoryMap = state.territoryMap;
		this.territoryMap.cellSize = this.mapSize / this.territoryMap.width;

		for (const i in this.gameState)
			this.gameState[i].update(this);

		// TODO: merge this with "ApplyEntitiesDelta" since after all they do the same.
		this.updateResourceMaps(this.events);

		Engine.ProfileStop();
	}

	ApplyEntitiesDelta(state)
	{
		Engine.ProfileStart("Shared ApplyEntitiesDelta");

		const foundationFinished = {};

		// by order of updating:
		// we "Destroy" last because we want to be able to switch Metadata first.

		for (const evt of state.events.Create)
		{
			if (!state.entities[evt.entity])
				continue; // Sometimes there are things like foundations which get destroyed too fast

			const entity = new Entity(this, state.entities[evt.entity]);
			this._entities.set(evt.entity, entity);
			this.entities.addEnt(entity);

			// Update all the entity collections since the create operation affects static properties as well as dynamic
			for (const entCol of this._entityCollections.values())
				entCol.updateEnt(entity);
		}

		for (const evt of state.events.EntityRenamed)
		{	// Switch the metadata: TODO entityCollections are updated only because of the owner change. Should be done properly
			for (const player of this._players)
			{
				this._entityMetadata[player][evt.newentity] = this._entityMetadata[player][evt.entity];
				this._entityMetadata[player][evt.entity] = {};
			}
		}

		for (const evt of state.events.TrainingFinished)
		{	// Apply metadata stored in training queues
			for (const entId of evt.entities)
				if (this._entities.has(entId))
					for (const key in evt.metadata)
						this.setMetadata(evt.owner, this._entities.get(entId), key, evt.metadata[key]);
		}

		for (const evt of state.events.ConstructionFinished)
		{
			// metada are already moved by EntityRenamed when needed (i.e. construction, not repair)
			if (evt.entity != evt.newentity)
				foundationFinished[evt.entity] = true;
		}

		for (const evt of state.events.AIMetadata)
		{
			if (!this._entities.has(evt.id))
				continue;	// might happen in some rare cases of foundations getting destroyed, perhaps.
			// Apply metadata (here for buildings for example)
			for (const key in evt.metadata)
				this.setMetadata(evt.owner, this._entities.get(evt.id), key, evt.metadata[key]);
		}

		for (const evt of state.events.Destroy)
		{
			if (foundationFinished[evt.entity])
				evt.SuccessfulFoundation = true;

			// The entity was destroyed but its data may still be useful, so
			// remember the AI's metadata concerning it
			evt.metadata = {};
			for (const player of this._players)
				evt.metadata[player] = this._entityMetadata[player][evt.entity];

			const entity = this._entities.get(evt.entity);
			if (entity)
			{
				for (const entCol of this._entityCollections.values())
					entCol.removeEnt(entity);
				this.entities.removeEnt(entity);
			}

			this._entities.delete(evt.entity);
			this._entitiesModifications.delete(evt.entity);
			for (const player of this._players)
				delete this._entityMetadata[player][evt.entity];
		}

		for (const id in state.entities)
		{
			const changes = state.entities[id];
			const entity = this._entities.get(+id);
			for (const prop in changes)
			{
				entity._entity[prop] = changes[prop];
				this.updateEntityCollections(prop, entity);
			}
		}

		// apply per-entity aura-related changes.
		// this supersedes tech-related changes.
		for (const id in state.changedEntityTemplateInfo)
		{
			if (!this._entities.has(+id))
				continue;	// dead, presumably.
			const changes = state.changedEntityTemplateInfo[id];
			if (!this._entitiesModifications.has(+id))
				this._entitiesModifications.set(+id, new Map());
			const modif = this._entitiesModifications.get(+id);
			for (const change of changes)
				modif.set(change.variable, change.value);
		}
		Engine.ProfileStop();
	}

	ApplyTemplatesDelta(state)
	{
		Engine.ProfileStart("Shared ApplyTemplatesDelta");

		for (const player in state.changedTemplateInfo)
		{
			const playerDiff = state.changedTemplateInfo[player];
			for (const template in playerDiff)
			{
				const changes = playerDiff[template];
				if (!this._templatesModifications[template])
					this._templatesModifications[template] = {};
				if (!this._templatesModifications[template][player])
					this._templatesModifications[template][player] = new Map();
				const modif = this._templatesModifications[template][player];
				for (const change of changes)
					modif.set(change.variable, change.value);
			}
		}
		this._templatesModifications =
			Object.fromEntries(Object.entries(this._templatesModifications).sort());
		Engine.ProfileStop();
	}

	registerUpdatingEntityCollection(entCollection)
	{
		entCollection.setUID(this._entityCollectionsUID);
		this._entityCollections.set(this._entityCollectionsUID, entCollection);
		for (const prop of entCollection.dynamicProperties())
		{
			if (!this._entityCollectionsByDynProp[prop])
				this._entityCollectionsByDynProp[prop] = new Map();
			this._entityCollectionsByDynProp[prop].set(this._entityCollectionsUID, entCollection);
		}
		this._entityCollectionsUID++;
	}

	removeUpdatingEntityCollection(entCollection)
	{
		const uid = entCollection.getUID();

		if (this._entityCollections.has(uid))
			this._entityCollections.delete(uid);

		for (const prop of entCollection.dynamicProperties())
			if (this._entityCollectionsByDynProp[prop].has(uid))
				this._entityCollectionsByDynProp[prop].delete(uid);
	}

	updateEntityCollections(property, ent)
	{
		if (this._entityCollectionsByDynProp[property] === undefined)
			return;

		for (const entCol of this._entityCollectionsByDynProp[property].values())
			entCol.updateEnt(ent);
	}

	setMetadata(player, ent, key, value)
	{
		let metadata = this._entityMetadata[player][ent.id()];
		if (!metadata)
		{
			this._entityMetadata[player][ent.id()] = {};
			metadata = this._entityMetadata[player][ent.id()];
		}
		metadata[key] = value;

		this.updateEntityCollections('metadata', ent);
		this.updateEntityCollections('metadata.' + key, ent);
	}

	getMetadata(player, ent, key)
	{
		return this._entityMetadata[player][ent.id()]?.[key];
	}

	deleteMetadata(player, ent, key)
	{
		const metadata = this._entityMetadata[player][ent.id()];

		if (!metadata || !(key in metadata))
			return true;
		metadata[key] = undefined;
		delete metadata[key];
		this.updateEntityCollections('metadata', ent);
		this.updateEntityCollections('metadata.' + key, ent);
		return true;
	}

	/** creates a map of resource density */
	createResourceMaps()
	{
		for (const resource of Resources.GetCodes())
		{
			if (this.resourceMaps[resource] ||
				!(Resources.GetResource(resource).aiAnalysisInfluenceGroup in this.normalizationFactor))
				continue;
			// We're creating them 8-bit. Things could go above 255 if there are really tons of resources
			// But at that point the precision is not really important anyway. And it saves memory.
			this.resourceMaps[resource] = new InfoMap(this, "resource");
			this.ccResourceMaps[resource] = new InfoMap(this, "resource");
		}
		for (const ent of this._entities.values())
			this.addEntityToResourceMap(ent);
	}

	/**
	 * @param {Object} events - The events from a turn.
	 */
	updateResourceMaps(events)
	{
		for (const e of events.Destroy)
		{
			const influence = this.resources.get(e.entity);
			if (!influence)
				continue;
			this.applyResourceInfluence(influence, -1);
			this.resources.delete(e.entity);
		}

		for (const e of events.Create)
			if (e.entity && this._entities.has(e.entity))
				this.addEntityToResourceMap(this._entities.get(e.entity));
	}

	/**
	 * @param {entity} entity - The entity to add to the resource map.
	 */
	addEntityToResourceMap(entity)
	{
		if (this.resources.has(entity.id()))
			return;
		const influence = this.getResourceInfluence(entity);
		if (!influence)
			return;
		this.applyResourceInfluence(influence, 1);
		this.resources.set(entity.id(), influence);
	}

	/**
	 * @param {entity} entity - The entity to remove from the resource map.
	 */
	removeEntityFromResourceMap(entity)
	{
		const influence = this.resources.get(entity.id());
		if (!influence)
			return;
		this.applyResourceInfluence(influence, -1);
		this.resources.delete(entity.id());
	}

	/**
	 * @param {entity} ent - A resource.
	 * @return {Object|undefined} - The influence of the resource on the resource maps, as
	 *	{ "resource", "x", "y", "strength" }, or undefined if it has none.
	 */
	getResourceInfluence(ent)
	{
		if (!ent)
			return undefined;
		const entPos = ent.position();
		if (!entPos)
			return undefined;
		const resource = ent.resourceSupplyType()?.generic;
		if (!resource || !this.resourceMaps[resource])
			return undefined;
		const [x, y] = this.resourceMaps[resource].gamePosToMapPos(entPos);
		const grp = Resources.GetResource(resource).aiAnalysisInfluenceGroup;
		return {
			"resource": resource,
			"x": x,
			"y": y,
			"strength": ent.resourceSupplyMax() / this.normalizationFactor[grp]
		};
	}

	/**
	 * Add (multiplication 1) or remove (-1) the influence of a resource on the resource maps.
	 * @param {Object} influence - As returned by getResourceInfluence.
	 */
	applyResourceInfluence({ resource, x, y, strength }, multiplication)
	{
		const cellSize = this.resourceMaps[resource].cellSize;
		const grp = Resources.GetResource(resource).aiAnalysisInfluenceGroup;
		const amount = multiplication * strength;
		this.resourceMaps[resource].addInfluence(x, y, this.influenceRadius[grp] / cellSize, amount / 2, "constant");
		this.resourceMaps[resource].addInfluence(x, y, this.influenceRadius[grp] / cellSize, amount / 2);
		this.ccResourceMaps[resource].addInfluence(x, y, this.ccInfluenceRadius[grp] / cellSize, amount, "constant");
	}
}
