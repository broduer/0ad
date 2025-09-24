/**
 * Contains the layout and button settings per selection panel
 *
 * getItems returns a list of basic items used to fill the panel.
 * This method is obligated. If the items list is empty, the panel
 * won't be rendered.
 *
 * Then there's a loop over all items provided. In the loop,
 * the item and some other standard data is added to a data object.
 *
 * The standard data is
 * {
 *   "i":              index
 *   "item":           item coming from the getItems function
 *   "playerState":    playerState
 *   "unitEntStates":  states of the selected entities
 *   "rowLength":      rowLength
 *   "numberOfItems":  number of items that will be processed
 *   "button":         gui Button object
 *   "icon":           gui Icon object
 *   "guiSelection":   gui button Selection overlay
 *   "countDisplay":   gui caption space
 * }
 *
 * Then for every data object, the setupButton function is called which
 * sets the view and handlers of the button.
 */

// Cache some formation info
// Available formations per player
var g_AvailableFormations = new Map();
var g_FormationsInfo = new Map();

var g_SelectionPanels = {};

var g_SelectionPanelBarterButtonManager;

g_SelectionPanels.Alert = {
	"getMaxNumberOfItems": function()
	{
		return 2;
	},
	"getItems": function(unitEntStates)
	{
		return unitEntStates.some(state => !!state.alertRaiser) ? ["raise", "end"] : [];
	},
	"setupButton": function(data)
	{
		data.button.onPress = function()
		{
			switch (data.item)
			{
			case "raise":
				raiseAlert();
				return;
			case "end":
				endOfAlert();
				return;
			default:
				error("Unknown value for alert action: " + data.item);
			}
		};

		switch (data.item)
		{
		case "raise":
			data.icon.sprite = "stretched:session/icons/bell_level1.png";
			data.button.tooltip = translate("Raise an alert!");
			if (data.unitEntStates.every(state => MatchesClassList(["Civilian"], state.alertRaiser?.classes)))
				data.button.tooltip += "\n" + bodyFont(translate("Alert nearby Civilians to seek refuge."));
			else if (data.unitEntStates.every(state => MatchesClassList(["Trader"], state.alertRaiser?.classes)))
				data.button.tooltip += "\n" + bodyFont(translate("Alert nearby Traders to seek refuge."));
			else
				data.button.tooltip += "\n" + bodyFont(translate("Alert nearby vulnerable units to seek refuge."));
			break;
		case "end":
			data.icon.sprite = "stretched:session/icons/bell_level0.png";
			data.button.tooltip = translate("End the alert.");
			if (data.unitEntStates.every(state => MatchesClassList(["Civilian"], state.alertRaiser?.classes)))
				data.button.tooltip += "\n" + bodyFont(translate("Unload nearby Civilians."));
			else if (data.unitEntStates.every(state => MatchesClassList(["Trader"], state.alertRaiser?.classes)))
				data.button.tooltip += "\n" + bodyFont(translate("Unload nearby Traders."));
			else
				data.button.tooltip += "\n" + bodyFont(translate("Unload nearby vulnerable units."));
			break;
		default:
			error("Unknown value for alert action: " + data.item);
		}
		data.button.enabled = controlsPlayer(data.player);

		setPanelObjectPosition(data.button, this.getMaxNumberOfItems() - data.i, data.rowLength);
		return true;
	}
};

g_SelectionPanels.Barter = {
	"getMaxNumberOfItems": function()
	{
		return 5;
	},
	"rowLength": 5,
	"conflictsWith": ["Garrison"],
	"getItems": function(unitEntStates)
	{
		// If more than `rowLength` resources, don't display icons.
		if (unitEntStates.every(state => !state.isBarterMarket) || g_ResourceData.GetBarterableCodes().length > this.rowLength)
			return [];
		return g_ResourceData.GetBarterableCodes();
	},
	"setupButton": function(data)
	{
		if (g_SelectionPanelBarterButtonManager)
		{
			g_SelectionPanelBarterButtonManager.setViewedPlayer(data.player);
			g_SelectionPanelBarterButtonManager.update();
		}
		return true;
	}
};

g_SelectionPanels.Command = {
	"getMaxNumberOfItems": function()
	{
		return 6;
	},
	"getItems": function(unitEntStates)
	{
		const commands = [];

		for (const command in g_EntityCommands)
		{
			const info = getCommandInfo(command, unitEntStates);
			if (!info)
				continue;

			info.name = command;
			if (commands.push(info) >= this.getMaxNumberOfItems())
				break;
		}
		return commands;
	},
	"setupButton": function(data)
	{
		data.button.tooltip = data.item.tooltip;

		data.button.onPress = function()
		{
			if (data.item.callback)
				data.item.callback(data.item);
			else
				performCommand(data.unitEntStates, data.item.name);
		};

		data.countDisplay.caption = data.item.count || "";

		data.button.enabled = data.item.enabled == true;

		data.icon.sprite = "stretched:session/icons/" + data.item.icon;

		const left = (data.i - data.numberOfItems / 2) * (data.button.size.bottom + 1);
		Object.assign(data.button.size, {
			// relative to the center ( = 50%)
			"rleft": 50,
			"rright": 50,
			// offset from the center calculation, count on square buttons, so size.bottom is the width too
			"left": left,
			"right": left + data.button.size.bottom
		});

		return true;
	}
};

g_SelectionPanels.Construction = {
	"getMaxNumberOfItems": function()
	{
		return 40 - getNumberOfRightPanelButtons();
	},
	"rowLength": 10,
	"getItems": function()
	{
		return getAllBuildableEntitiesFromSelection();
	},
	"setupButton": function(data)
	{
		const template = GetTemplateData(data.item, data.player);
		if (!template)
			return false;

		const requirementsMet = Engine.GuiInterfaceCall("AreRequirementsMet", {
			"requirements": template.requirements,
			"player": data.player
		});

		let neededResources;
		if (template.cost)
			neededResources = Engine.GuiInterfaceCall("GetNeededResources", {
				"cost": multiplyEntityCosts(template, 1),
				"player": data.player
			});

		data.button.onPress = function() { startBuildingPlacement(data.item, data.playerState); };
		const showTemplateFunc = () => { showTemplateDetails(data.item, data.playerState.civ); };
		data.button.onPressRight = showTemplateFunc;
		data.button.onPressRightDisabled = showTemplateFunc;

		const tooltips = [
			getEntityNamesFormatted,
			getVisibleEntityClassesFormatted,
			getAurasTooltip,
			getEntityTooltip
		].map(func => func(template));
		tooltips.push(
			getEntityCostTooltip(template, data.player),
			getResourceDropsiteTooltip(template),
			getGarrisonTooltip(template),
			getTurretsTooltip(template),
			getPopulationBonusTooltip(template),
			getTemplateViewerOnRightClickTooltip(template)
		);


		const limits = getEntityLimitAndCount(data.playerState, data.item);
		tooltips.push(
			formatLimitString(limits.entLimit, limits.entCount, limits.entLimitChangers),
			formatMatchLimitString(limits.matchLimit, limits.matchCount, limits.type),
			getRequirementsTooltip(requirementsMet, template.requirements, GetSimState().players[data.player].civ),
			getNeededResourcesTooltip(neededResources));

		data.button.tooltip = tooltips.filter(tip => tip).join("\n");

		let modifier = "";
		if (!requirementsMet || limits.canBeAddedCount == 0)
		{
			data.button.enabled = false;
			modifier += "color:0 0 0 127:grayscale:";
		}
		else if (neededResources)
		{
			data.button.enabled = false;
			modifier += resourcesToAlphaMask(neededResources) + ":";
		}
		else
			data.button.enabled = controlsPlayer(data.player);

		if (template.icon)
			data.icon.sprite = modifier + "stretched:session/portraits/" + template.icon;

		setPanelObjectPosition(data.button, data.i + getNumberOfRightPanelButtons(), data.rowLength);
		return true;
	}
};

g_SelectionPanels.Formation = {
	"getMaxNumberOfItems": function()
	{
		return 15;
	},
	"rowLength": 5,
	"conflictsWith": ["Garrison"],
	"getItems": function(unitEntStates)
	{
		if (unitEntStates.some(state => !hasClass(state, "Unit")))
			return [];

		if (unitEntStates.every(state => !state.unitAI || !state.unitAI.formations.length))
			return [];

		if (!g_AvailableFormations.has(unitEntStates[0].player))
			g_AvailableFormations.set(unitEntStates[0].player, Engine.GuiInterfaceCall("GetAvailableFormations", unitEntStates[0].player));

		return g_AvailableFormations.get(unitEntStates[0].player).filter(formation => unitEntStates.some(state => !!state.unitAI && state.unitAI.formations.includes(formation)));
	},
	"setupButton": function(data)
	{
		if (!g_FormationsInfo.has(data.item))
			g_FormationsInfo.set(data.item, Engine.GuiInterfaceCall("GetFormationInfoFromTemplate", { "templateName": data.item }));

		const formationOk = canMoveSelectionIntoFormation(data.item);
		const unitIds = data.unitEntStates.map(state => state.id);
		const formationSelected = Engine.GuiInterfaceCall("IsFormationSelected", {
			"ents": unitIds,
			"formationTemplate": data.item
		});

		data.button.onPress = function()
		{
			performFormation(unitIds, data.item);
		};

		data.button.onMouseRightPress = () => g_AutoFormation.setDefault(data.item);

		const formationInfo = g_FormationsInfo.get(data.item);
		let tooltip = translate(formationInfo.name);
		if (formationInfo.tooltip)
			tooltip += "\n" + bodyFont(translate(formationInfo.tooltip));

		const isDefaultFormation = g_AutoFormation.isDefault(data.item);
		if (data.item === NULL_FORMATION)
			tooltip += "\n" + (isDefaultFormation ?
				translate("Default formation is disabled.") :
				translate("Right-click to disable the default formation feature."));
		else
			tooltip += "\n" + (isDefaultFormation ?
				translate("This is the default formation, used for movement orders.") :
				translate("Right-click to set this as the default formation."));

		if (!formationOk && formationInfo.disabledTooltip)
			tooltip += "\n" + objectionFont(translate(formationInfo.disabledTooltip));
		data.button.tooltip = tooltip;

		data.button.enabled = formationOk && controlsPlayer(data.player);
		const grayscale = formationOk ? "" : "grayscale:";
		data.guiSelection.hidden = !formationSelected;
		data.countDisplay.hidden = !isDefaultFormation;
		data.icon.sprite = "stretched:" + grayscale + "session/icons/" + formationInfo.icon;

		setPanelObjectPosition(data.button, data.i, data.rowLength);
		return true;
	}
};

g_SelectionPanels.Garrison = {
	"getMaxNumberOfItems": function()
	{
		return 12;
	},
	"rowLength": 4,
	"conflictsWith": ["Barter"],
	"getItems": function(unitEntStates)
	{
		if (unitEntStates.every(state => !state.garrisonHolder))
			return [];

		const groups = new EntityGroups();

		for (const state of unitEntStates)
			if (state.garrisonHolder)
				groups.add(state.garrisonHolder.entities);

		return groups.getEntsGrouped();
	},
	"setupButton": function(data)
	{
		const entState = GetEntityState(data.item.ents[0]);

		const template = GetTemplateData(entState.template);
		if (!template)
			return false;

		data.button.onPress = function()
		{
			unloadTemplate(template.selectionGroupName || entState.template, entState.player);
		};

		data.countDisplay.caption = data.item.ents.length > 1 ? data.item.ents.length : "";

		const canUngarrison = controlsPlayer(data.player) || controlsPlayer(entState.player);

		data.button.enabled = canUngarrison;

		data.button.tooltip = (canUngarrison ?
			sprintf(translate("Unload %(name)s"), { "name": getEntityNames(template) }) + "\n" +
			translate("Single-click to unload 1. Shift-click to unload all of this type.") :
			getEntityNames(template)) + "\n" +
			sprintf(translate("Player: %(playername)s"), {
				"playername": g_Players[entState.player].name
			});

		data.guiSelection.sprite = "color:" + g_DiplomacyColors.getPlayerColor(entState.player, 160);
		data.button.sprite_disabled = data.button.sprite;

		// Selection panel buttons only appear disabled if they
		// also appear disabled to the owner of the structure.
		data.icon.sprite =
			(canUngarrison || g_IsObserver ? "" : "grayscale:") +
			"stretched:session/portraits/" + template.icon;

		setPanelObjectPosition(data.button, data.i, data.rowLength);

		return true;
	}
};

g_SelectionPanels.Gate = {
	"getMaxNumberOfItems": function()
	{
		return 40 - getNumberOfRightPanelButtons();
	},
	"rowLength": 10,
	"getItems": function(unitEntStates)
	{
		const hideLocked = unitEntStates.every(state => !state.gate || !state.gate.locked);
		const hideUnlocked = unitEntStates.every(state => !state.gate || state.gate.locked);

		if (hideLocked && hideUnlocked)
			return [];

		return [
			{
				"hidden": hideLocked,
				"tooltip": translate("Lock Gate"),
				"icon": "session/icons/lock_locked.png",
				"locked": true
			},
			{
				"hidden": hideUnlocked,
				"tooltip": translate("Unlock Gate"),
				"icon": "session/icons/lock_unlocked.png",
				"locked": false
			}
		];
	},
	"setupButton": function(data)
	{
		data.button.onPress = function() { lockGate(data.item.locked); };
		data.button.tooltip = data.item.tooltip;
		data.button.enabled = controlsPlayer(data.player);
		data.guiSelection.hidden = data.item.hidden;
		data.icon.sprite = "stretched:" + data.item.icon;

		setPanelObjectPosition(data.button, data.i + getNumberOfRightPanelButtons(), data.rowLength);
		return true;
	}
};

g_SelectionPanels.Pack = {
	"getMaxNumberOfItems": function()
	{
		return 40 - getNumberOfRightPanelButtons();
	},
	"rowLength": 10,
	"getItems": function(unitEntStates)
	{
		const checks = {};
		for (const state of unitEntStates)
		{
			if (!state.pack)
				continue;

			if (state.pack.progress == 0)
			{
				if (state.pack.packed)
					checks.unpackButton = true;
				else
					checks.packButton = true;
			}
			else if (state.pack.packed)
				checks.unpackCancelButton = true;
			else
				checks.packCancelButton = true;
		}

		const items = [];
		if (checks.packButton)
			items.push({
				"packing": false,
				"packed": false,
				"tooltip": translate("Pack"),
				"callback": function() { packUnit(true); }
			});

		if (checks.unpackButton)
			items.push({
				"packing": false,
				"packed": true,
				"tooltip": translate("Unpack"),
				"callback": function() { packUnit(false); }
			});

		if (checks.packCancelButton)
			items.push({
				"packing": true,
				"packed": false,
				"tooltip": translate("Cancel Packing"),
				"callback": function() { cancelPackUnit(true); }
			});

		if (checks.unpackCancelButton)
			items.push({
				"packing": true,
				"packed": true,
				"tooltip": translate("Cancel Unpacking"),
				"callback": function() { cancelPackUnit(false); }
			});

		return items;
	},
	"setupButton": function(data)
	{
		data.button.onPress = function() {data.item.callback(data.item); };

		data.button.tooltip = data.item.tooltip;

		if (data.item.packing)
			data.icon.sprite = "stretched:session/icons/cancel.png";
		else if (data.item.packed)
			data.icon.sprite = "stretched:session/icons/unpack.png";
		else
			data.icon.sprite = "stretched:session/icons/pack.png";

		data.button.enabled = controlsPlayer(data.player);

		setPanelObjectPosition(data.button, data.i + getNumberOfRightPanelButtons(), data.rowLength);
		return true;
	}
};

g_SelectionPanels.Queue = {
	"getMaxNumberOfItems": function()
	{
		return 16;
	},
	/**
	 * Returns a list of all items in the productionqueue of the selection
	 * The first entry of every entity's production queue will come before
	 * the second entry of every entity's production queue
	 */
	"getItems": function(unitEntStates)
	{
		const queue = [];
		let foundNew = true;
		for (let i = 0; foundNew; ++i)
		{
			foundNew = false;
			for (const state of unitEntStates)
			{
				if (!state.production || !state.production.queue[i])
					continue;
				queue.push({
					"producingEnt": state.id,
					"queuedItem": state.production.queue[i],
					"autoqueue": state.production.autoqueue && state.production.queue[i].unitTemplate,
				});
				foundNew = true;
			}
		}
		if (!queue.length)
			return queue;
		// Add 'ghost' items to show autoqueues.
		const repeat = [];
		for (const item of queue)
			if (item.autoqueue)
			{
				const ghostItem = clone(item);
				ghostItem.ghost = true;
				repeat.push(ghostItem);
			}
		if (repeat.length)
			for (let i = 0; queue.length < g_SelectionPanels.Queue.getMaxNumberOfItems(); ++i)
				queue.push(repeat[i % repeat.length]);
		return queue;
	},
	"resizePanel": function(numberOfItems, rowLength)
	{
		const numRows = Math.ceil(numberOfItems / rowLength);
		const panel = Engine.GetGUIObjectByName("unitQueuePanel");
		const buttonSize = Engine.GetGUIObjectByName("unitQueueButton[0]").size.bottom;
		const margin = 4;
		panel.size.top = panel.size.bottom - numRows * buttonSize - (numRows + 2) * margin;
	},
	"setupButton": function(data)
	{
		const queuedItem = data.item.queuedItem;

		// Differentiate between units and techs
		let template;
		if (queuedItem.unitTemplate)
			template = GetTemplateData(queuedItem.unitTemplate);
		else if (queuedItem.technologyTemplate)
			template = GetTechnologyData(queuedItem.technologyTemplate, GetSimState().players[data.player].civ);
		else
		{
			warning("Unknown production queue template " + uneval(queuedItem));
			return false;
		}
		data.button.onPress = function() { removeFromProductionQueue(data.item.producingEnt, queuedItem.id); };

		const tooltips = [getEntityNames(template)];
		if (data.item.ghost)
			tooltips.push(translate("The auto-queue will try to train this item later."));
		if (queuedItem.neededSlots)
		{
			tooltips.push(objectionFont(translate("Insufficient population capacity:")));
			tooltips.push(sprintf(translate("%(population)s %(neededSlots)s"), {
				"population": resourceIcon("population"),
				"neededSlots": queuedItem.neededSlots
			}));
		}
		tooltips.push(getTemplateViewerOnRightClickTooltip(template));
		data.button.tooltip = tooltips.join("\n");

		data.countDisplay.caption = queuedItem.count > 1 ? queuedItem.count : "";

		const progressSlider = Engine.GetGUIObjectByName("unitQueueProgressSlider[" + data.i + "]");
		if (data.item.ghost)
		{
			data.button.enabled = false;
			progressSlider.sprite = "color:0 150 250 50";

			// Buttons are assumed to be square, so left/right offsets can be used for top/bottom.
			progressSlider.size.top = progressSlider.size.left;
		}
		else
		{
			// Show the time remaining to finish the first item
			if (data.i == 0)
				Engine.GetGUIObjectByName("queueTimeRemaining").caption =
					Engine.FormatMillisecondsIntoDateStringGMT(queuedItem.timeRemaining, translateWithContext("countdown format", "m:ss"));

			progressSlider.sprite = "queueProgressSlider";

			// Buttons are assumed to be square, so left/right offsets can be used for top/bottom.
			progressSlider.size.top = progressSlider.size.left + Math.round(queuedItem.progress * (progressSlider.size.right - progressSlider.size.left));

			data.button.enabled = controlsPlayer(data.player);

			Engine.GetGUIObjectByName("unitQueuePausedIcon[" + data.i + "]").hidden = !queuedItem.paused;
			if (queuedItem.paused)
				// Translation: String displayed when the research is paused. E.g. by being garrisoned or when not the first item in the queue.
				data.button.tooltip += "\n" + translate("This item is paused.");
		}

		if (template.icon)
		{
			let modifier = "stretched:";
			if (queuedItem.paused)
				modifier += "color:0 0 0 127:grayscale:";
			else if (data.item.ghost)
				modifier += "grayscale:";
			data.icon.sprite = modifier + "session/portraits/" + template.icon;
		}


		const showTemplateFunc = () => { showTemplateDetails(data.item.queuedItem.unitTemplate || data.item.queuedItem.technologyTemplate, data.playerState.civ); };
		data.button.onPressRight = showTemplateFunc;
		data.button.onPressRightDisabled = showTemplateFunc;

		setPanelObjectPosition(data.button, data.i, data.rowLength);
		return true;
	}
};

g_SelectionPanels.Research = {
	"getMaxNumberOfItems": function()
	{
		return 10;
	},
	"rowLength": 10,
	"init": function()
	{
		const updateAffectsIconVisibility = () =>
		{
			this.helper.showAffectsIcons = Engine.ConfigDB_GetValue("user", "gui.session.techarrows") === "true";
		};
		registerConfigChangeHandler(changes =>
		{
			if (changes.has("gui.session.techarrows"))
				updateAffectsIconVisibility();
			// They will be rerendered with the new visibility next frame.
		});
		updateAffectsIconVisibility();
	},
	"reset": function()
	{
		this.helper.occupiedPositions = new Set();
		this.helper.bottomRowButtonCount = 0;
	},
	"getItems": function(unitEntStates)
	{
		if (getNumberOfRightPanelButtons() >= this.rowLength * 2)
			return [];

		let ret = [];
		if (unitEntStates.length == 1)
		{
			const entState = unitEntStates[0];
			if (!entState?.researcher?.technologies)
				return ret;
			if (!entState.production)
				warn("Researcher without ProductionQueue found: " + entState.id + ".");
			return entState.researcher.technologies.map(tech => ({
				"tech": tech,
				"techCostMultiplier": entState.researcher.techCostMultiplier,
				"researchFacilityId": entState.id,
				"isUpgrading": !!entState.upgrade && entState.upgrade.isUpgrading
			}));
		}

		const sortedEntStates = unitEntStates.sort((a, b) =>
			(!b.upgrade || !b.upgrade.isUpgrading) - (!a.upgrade || !a.upgrade.isUpgrading) ||
			(!a.production ? 0 : a.production.queue.length) - (!b.production ? 0 : b.production.queue.length)
		);

		for (const state of sortedEntStates)
		{
			if (!state.researcher || !state.researcher.technologies)
				continue;
			if (!state.production)
				warn("Researcher without ProductionQueue found: " + state.id + ".");

			// Remove the techs we already have in ret (with the same name and techCostMultiplier)
			const filteredTechs = state.researcher.technologies.filter(
				tech => tech != null && !ret.some(
					item =>
						(item.tech == tech ||
							item.tech.pair &&
							tech.pair &&
							item.tech.pair?.[0] == tech.pair?.[0] &&
							item.tech.pair?.[1] == tech.pair?.[1]) &&
						Object.keys(item.techCostMultiplier).every(
							k => item.techCostMultiplier[k] == state.researcher.techCostMultiplier[k])
				));

			if (filteredTechs.length + ret.length <= this.getMaxNumberOfItems())
				ret = ret.concat(filteredTechs.map(tech => ({
					"tech": tech,
					"techCostMultiplier": state.researcher.techCostMultiplier,
					"researchFacilityId": state.id,
					"isUpgrading": !!state.upgrade && state.upgrade.isUpgrading
				})));
		}
		return ret;
	},
	"hideItem": function(i, rowLength) // Called when no item is found
	{
		Engine.GetGUIObjectByName("unitResearchButton[" + i + "]").hidden = true;
		// Remove the button it would have been paired with as well.
		Engine.GetGUIObjectByName("unitResearchButton[" + (i + this.getMaxNumberOfItems()) + "]").hidden = true;
	},
	"setupButton": function(data)
	{
		if (!data.item.tech)
		{
			this.hideItem(data.i, data.rowLength);
			return false;
		}

		// There are twice as many button objects than this.getMaxNumberOfItems()
		// This is because each item could be a tech pair and need a second one in addition to the one at data.i
		data.j = data.i + this.getMaxNumberOfItems();

		const playerState = GetSimState().players[data.player];

		if (data.item.tech.pair)
		{
			const firstTemplate = GetTechnologyData(data.item.tech.pair[0], playerState.civ);
			const secondTemplate = GetTechnologyData(data.item.tech.pair[1], playerState.civ);

			// template.reqs is false if the tech isn't researchable by the current civ.
			const firstResearchable = !!firstTemplate?.reqs;
			const secondResearchable = !!secondTemplate?.reqs;

			if (firstResearchable && secondResearchable)
				// Ideal/expected case: Display both techs in a pair.
				return this.helper.setupButtonPair(data, data.item.tech.pair[0], data.item.tech.pair[1], firstTemplate,
					secondTemplate, playerState);

			// At least one of the two is not valid or researchable. If the other one is, display it as a single tech
			// on its own.
			if (firstResearchable && !secondResearchable)
				return this.helper.setupSingleButton(data, data.item.tech.pair[0], firstTemplate, playerState);
			if (!firstResearchable && secondResearchable)
				return this.helper.setupSingleButton(data, data.item.tech.pair[1], secondTemplate, playerState);

			// Neither of the two are valid and researchable.
			this.hideItem(data.i, data.rowLength);
			return false;
		}

		const template = GetTechnologyData(data.item.tech, playerState.civ);
		// template.reqs is false if the tech isn't researchable by the current civ.
		if (template?.reqs)
			return this.helper.setupSingleButton(data, data.item.tech, template, playerState);

		this.hideItem(data.i, data.rowLength);
		return false;
	},
	"helper": {
		// Techs can optionally define a placeBelow property that specifies a unit whose training button they want to be placed below.
		// It can be:
		// 		- "{UnlockedUnit}": the first unit whose requirements (of the Identity component) contain the tech.
		// 		- "{AffectedUnit}": the first unit whose stats are modified (receives buffs or debuffs) by the tech.
		// 		- class combination: the first unit whose identity classes match that combination.
		"findTargetTrainingButton": function(data, techName, template)
		{
			// Also check whether the other right panel buttons (training, constructing, upgrading) reach the second row.
			// In that case, we want to place all techs in the bottom row. Research buttons should never be placed in the
			// same row as these.
			if (!template.placeBelow || getNumberOfRightPanelButtons() > data.rowLength)
				return -1;

			const indices = [];
			if (template.placeBelow === "{UnlockedUnit}")
			{
				getAllTrainableEntitiesFromSelection().forEach((trainableTemplate, i) =>
				{
					if (GetTemplateData(trainableTemplate, data.player)?.requirements?.Techs?._string.split(/\s+/).includes(techName))
						indices.push(i);
				});
			}
			else
			{
				let targetClassList;
				if (template.placeBelow === "{AffectedUnit}")
				{
					const affectsList = (template.affects || []);
					for (const mod of template.modifications)
						if (mod.affects)
							affectsList.push(mod.affects);

					targetClassList = affectsList.map(classes => classes.split(/\s+/));
				}
				else
					targetClassList = [template.placeBelow.split(/\s+/)];

				getAllTrainableEntitiesFromSelection().forEach((trainableTemplate, i) =>
				{
					if (MatchesClassList(GetTemplateData(trainableTemplate, data.player).visibleIdentityClasses, targetClassList))
						indices.push(i);
				});
			}
			// Only choose a training button if it's the only matching one.
			if (indices.length !== 1)
				return -1;

			// Make sure to account for the other buttons placed before the unit training ones.
			return indices[0] + ["Construction", "Pack", "Gate", "Upgrade"].reduce((total, panel) =>
				total + g_unitPanelButtons[panel], 0
			);

		},
		"setupSingleButton": function(data, techName, template, playerState)
		{
			// The item is not a tech pair. So hide the button that data.button would have been paired with.
			Engine.GetGUIObjectByName("unitResearchButton[" + data.j + "]").hidden = true;

			// Note: The GUI object container of the research buttons (unlike the one of the training buttons) only reaches up to the second row.
			// This means that, for example, a research button with position 5 is located directly one row under a training button with position 5.
			let position = this.findTargetTrainingButton(data, techName, template);

			let placeInBottomRow = position == -1;
			if (!placeInBottomRow && this.occupiedPositions.has(position))
			{
				// Try to fall back to the third (second-to-bottom) row.
				position += data.rowLength;
				if (this.occupiedPositions.has(position))
					// Both positions below the target unit are already used by other techs.
					// Note: Ideally this should never occur. Two techs per unit should be the limit. This here is just edge case handling.
					placeInBottomRow = true;
			}
			if (placeInBottomRow)
			{
				// Try to move it to the fourth (bottom) row.
				if (this.bottomRowButtonCount >= data.rowLength)
					return false; // Bottom row is full, we can't display it.
				position = this.bottomRowButtonCount + data.rowLength * 2;
			}

			Engine.GetGUIObjectByName("unitResearchVerticalPairIcon[" + data.i + "]").hidden = true;
			Engine.GetGUIObjectByName("unitResearchHorizontalPairIcon[" + data.i + "]").hidden = true;

			// When it's not "active", it's grayed out.
			const buttonActive = this.buildButton(data, techName, template, position, playerState, data.button, data.icon);
			this.buildAffectsIcon(data.i, !placeInBottomRow, buttonActive);

			return true;
		},
		"setupButtonPair": function(data, firstTechName, secondTechName, firstTemplate, secondTemplate, playerState)
		{
			// Note: The GUI object container of the research buttons (unlike the one of the training buttons) only
			// reaches up to the second row. This means that, for example, a research button with position 5 is located
			// directly one row under a training button with position 5.
			let firstPosition = this.findTargetTrainingButton(data, firstTechName, firstTemplate);
			let secondPosition = this.findTargetTrainingButton(data, secondTechName, secondTemplate);

			// Possible placements of tech pair with descending preference:
			// 	- Vertically below a single unit.
			//  - Horizontally below two adjacent units.
			//  - Horizontally adjacent below no unit in the bottom row.

			// Only ever place either below a unit, if the other can be too and below the same or an adjacent one.
			let placeInBottomRow = firstPosition == -1 || secondPosition == -1 || Math.abs(firstPosition - secondPosition) > 1;
			let placeHorizontally = true;
			if (!placeInBottomRow && firstPosition === secondPosition)
			{
				// Both want to be placed under the same unit.
				// Try to place the pair vertically by moving the second one down to the third (second-to-bottom) row,
				// below the first one.
				secondPosition += data.rowLength;
				if (this.occupiedPositions.has(firstPosition) || this.occupiedPositions.has(secondPosition))
					placeInBottomRow = true;
				else
					placeHorizontally = false;
			}
			else if (!placeInBottomRow && (this.occupiedPositions.has(firstPosition) || this.occupiedPositions.has(secondPosition)))
			{
				// At least one of the two respective positions in the second (third-to-bottom) row is occupied.
				// So try move both to the third.
				firstPosition += data.rowLength;
				secondPosition += data.rowLength;
				if (this.occupiedPositions.has(firstPosition) || this.occupiedPositions.has(secondPosition))
					// Neither the two positions in the second row nor the third row below the target training buttons
					// are available.
					placeInBottomRow = true;
			}

			if (placeInBottomRow)
			{
				// Try to move both to the bottom row.
				if (this.bottomRowButtonCount >= data.rowLength - 1)
					// Not enough space in the bottom row for both of them. We can't display them.
					return false;

				firstPosition = this.bottomRowButtonCount + data.rowLength * 2;
				secondPosition = firstPosition + 1;
			}

			// Note: the button indices here aren't related to positioning at all.
			const firstButtonIndex = data.i;
			const secondButtonIndex = data.j;
			const firstButton = data.button;
			const secondButton = Engine.GetGUIObjectByName("unitResearchButton[" + secondButtonIndex + "]");
			const firstIcon = data.icon;
			const secondIcon = Engine.GetGUIObjectByName("unitResearchIcon[" + secondButtonIndex + "]");

			// When it's not "active", it's grayed out.
			const firstButtonActive = this.buildButton(data, firstTechName, firstTemplate, firstPosition, playerState,
				firstButton, firstIcon);
			this.buildAffectsIcon(firstButtonIndex, !placeInBottomRow, firstButtonActive);

			// When it's not "active", it's grayed out.
			const secondButtonActive = this.buildButton(data, secondTechName, secondTemplate, secondPosition, playerState,
				secondButton, secondIcon);
			this.buildAffectsIcon(secondButtonIndex, !placeInBottomRow && placeHorizontally, secondButtonActive);

			this.buildPairIcon(false, firstButtonIndex, placeHorizontally && secondPosition > firstPosition, firstButtonActive);
			this.buildPairIcon(false, secondButtonIndex, placeHorizontally && secondPosition < firstPosition, secondButtonActive);
			this.buildPairIcon(true, firstButtonIndex, !placeHorizontally, firstButtonActive);
			this.buildPairIcon(true, secondButtonIndex, false, secondButtonActive);

			// While hovering over either button, show a cross over the other one.
			// TODO: The following lines have to be executed only once, technically, and not every this function is called.
			const firstUnchosenIcon = Engine.GetGUIObjectByName("unitResearchUnchosenIcon[" + firstButtonIndex + "]");
			const secondUnchosenIcon = Engine.GetGUIObjectByName("unitResearchUnchosenIcon[" + secondButtonIndex + "]");
			firstButton.onMouseEnter = () => { secondUnchosenIcon.hidden = false; };
			firstButton.onMouseLeave = () => { secondUnchosenIcon.hidden = true; };
			secondButton.onMouseEnter = () => { firstUnchosenIcon.hidden = false; };
			secondButton.onMouseLeave = () => { firstUnchosenIcon.hidden = true; };

			return true;
		},
		"buildAffectsIcon": function(i, show, enable)
		{
			const icon = Engine.GetGUIObjectByName("unitResearchAffectsIcon[" + i + "]");
			const hidden = !show || !this.showAffectsIcons;
			icon.hidden = hidden;
			if (!hidden)
				icon.sprite = "stretched:session/icons/" + (enable ? "tech_affects.png" : "tech_affects_disabled.png");
		},
		"buildPairIcon": function(vertical, i, show, enable)
		{
			const icon = Engine.GetGUIObjectByName("unitResearch" + (vertical ? "Vertical" : "Horizontal") + "PairIcon[" + i + "]");
			icon.hidden = !show;
			if (show)
				icon.sprite = "stretched:session/icons/" +
					(vertical ?
						enable ? "vertical_tech_pair.png" : "vertical_tech_pair_disabled.png" :
						enable ? "horizontal_tech_pair.png" : "horizontal_tech_pair_disabled.png");
		},
		"buildButton": function(baseData, techName, template, position, playerState, button, icon)
		{
			// Make sure to not modify the original template.
			const adaptedTemplate = clone(template);
			for (const res in adaptedTemplate.cost)
				adaptedTemplate.cost[res] *=
					baseData.item.techCostMultiplier[res] !== undefined ? baseData.item.techCostMultiplier[res] : 1;

			const neededResources = Engine.GuiInterfaceCall("GetNeededResources", {
				"cost": adaptedTemplate.cost,
				"player": baseData.player
			});

			const requirementsPassed = Engine.GuiInterfaceCall("CheckTechnologyRequirements", {
				"tech": techName,
				"player": baseData.player
			});

			const tooltips = [
				getEntityNamesFormatted,
				getEntityTooltip,
				getEntityCostTooltip,
				getTemplateViewerOnRightClickTooltip
			].map(func => func(adaptedTemplate));

			if (!requirementsPassed)
			{
				let tip = adaptedTemplate.requirementsTooltip;
				const reqs = adaptedTemplate.reqs;
				for (const req of reqs)
				{
					if (!req.entities)
						continue;

					const entityCounts = [];
					for (const entity of req.entities)
					{
						let current = 0;
						switch (entity.check)
						{
						case "count":
							current = playerState.classCounts[entity.class] || 0;
							break;

						case "variants":
							current = playerState.typeCountsByClass[entity.class] ?
								Object.keys(playerState.typeCountsByClass[entity.class]).length : 0;
							break;
						default:
							error("Unknow value in entity requirement check: " + entity.check);
						}

						const remaining = entity.number - current;
						if (remaining < 1)
							continue;

						entityCounts.push(sprintf(translatePlural("%(number)s entity of class %(class)s", "%(number)s entities of class %(class)s", remaining), {
							"number": remaining,
							"class": translate(entity.class)
						}));
					}

					tip += " " + sprintf(translate("Remaining: %(entityCounts)s"), {
						"entityCounts": entityCounts.join(translateWithContext("Separator for a list of entity counts", ", "))
					});
				}
				tooltips.push(objectionFont(tip));
			}
			tooltips.push(getNeededResourcesTooltip(neededResources));
			button.tooltip = tooltips.filter(tip => tip).join("\n");

			button.onPress = (t => function()
			{
				addResearchToQueue(baseData.item.researchFacilityId, t);
			})(techName);

			const showTemplateFunc = (t => function()
			{
				showTemplateDetails(
					t,
					GetTemplateData(baseData.unitEntStates.find(state => state.id == baseData.item.researchFacilityId).template).nativeCiv);
			});

			button.onPressRight = showTemplateFunc(techName);
			button.onPressRightDisabled = showTemplateFunc(techName);

			button.hidden = false;
			let modifier = "";
			let isActive = true;
			if (!requirementsPassed)
			{
				button.enabled = false;
				modifier += "color:0 0 0 127:grayscale:";
				isActive = false;
			}
			else if (neededResources)
			{
				button.enabled = false;
				modifier += resourcesToAlphaMask(neededResources) + ":";
			}
			else
				button.enabled = controlsPlayer(baseData.player);

			if (baseData.item.isUpgrading)
			{
				button.enabled = false;
				modifier += "color:0 0 0 127:grayscale:";
				isActive = false;
				button.tooltip += "\n" + objectionFont(translate("Cannot research while upgrading."));
			}

			if (adaptedTemplate.icon)
				icon.sprite = modifier + "stretched:session/portraits/" + adaptedTemplate.icon;

			this.occupiedPositions.add(position);
			if (position >= 2 * baseData.rowLength)
				this.bottomRowButtonCount++;

			// The panel is a bit higher than 4 * baseData.rowLength, which allows us to visibility anchor the buttons
			// in the bottom row to the bottom by moving them down those few pixels. Else the gap would be at the bottom.
			// This creates a small spatial separation between the "generic" techs in the bottom row and the "specific"
			// techs above them.
			const vOffset = position >= baseData.rowLength * 2 ? 6 : 0;
			setPanelObjectPosition(button, position, baseData.rowLength, 1, 1, vOffset);

			return isActive;
		}
	}
};

g_SelectionPanels.Selection = {
	"getMaxNumberOfItems": function()
	{
		return 16;
	},
	"rowLength": 4,
	"getItems": function(unitEntStates)
	{
		if (unitEntStates.length < 2)
			return [];
		return g_Selection.groups.getEntsGrouped();
	},
	"setupButton": function(data)
	{
		const entState = GetEntityState(data.item.ents[0]);
		const template = GetTemplateData(entState.template);
		if (!template)
			return false;

		for (const ent of data.item.ents)
		{
			const state = GetEntityState(ent);

			if (state.resourceCarrying && state.resourceCarrying.length !== 0)
			{
				if (!data.carried)
					data.carried = {};
				const carrying = state.resourceCarrying[0];
				if (data.carried[carrying.type])
					data.carried[carrying.type] += carrying.amount;
				else
					data.carried[carrying.type] = carrying.amount;
			}

			if (state.trader && state.trader.goods && state.trader.goods.amount)
			{
				if (!data.carried)
					data.carried = {};
				const amount = state.trader.goods.amount;
				const type = state.trader.goods.type;
				let totalGain = amount.traderGain;
				if (amount.market1Gain)
					totalGain += amount.market1Gain;
				if (amount.market2Gain)
					totalGain += amount.market2Gain;
				if (data.carried[type])
					data.carried[type] += totalGain;
				else
					data.carried[type] = totalGain;
			}
		}

		const unitOwner = GetEntityState(data.item.ents[0]).player;
		let tooltip = getEntityNames(template);
		if (data.carried)
			tooltip += "\n" + Object.keys(data.carried).map(res =>
				resourceIcon(res) + data.carried[res]
			).join(" ");
		if (g_IsObserver)
			tooltip += "\n" + sprintf(translate("Player: %(playername)s"), {
				"playername": g_Players[unitOwner].name
			});
		data.button.tooltip = tooltip;

		data.guiSelection.sprite = "color:" + g_DiplomacyColors.getPlayerColor(unitOwner, 160);
		data.guiSelection.hidden = !g_IsObserver;

		data.countDisplay.caption = data.item.ents.length > 1 ? data.item.ents.length : "";

		data.button.onPress = function()
		{
			if (Engine.HotkeyIsPressed("session.deselectgroup"))
				removeFromSelectionGroup(data.item.key);
			else
				makePrimarySelectionGroup(data.item.key);
		};
		data.button.onPressRight = function() { removeFromSelectionGroup(data.item.key); };

		if (template.icon)
			data.icon.sprite = "stretched:session/portraits/" + template.icon;

		setPanelObjectPosition(data.button, data.i, data.rowLength);
		return true;
	}
};

g_SelectionPanels.Stance = {
	"getMaxNumberOfItems": function()
	{
		return 5;
	},
	"getItems": function(unitEntStates)
	{
		if (unitEntStates.some(state => !state.unitAI || !hasClass(state, "Unit") || hasClass(state, "Animal")))
			return [];

		return unitEntStates[0].unitAI.selectableStances;
	},
	"setupButton": function(data)
	{
		const unitIds = data.unitEntStates.map(state => state.id);
		data.button.onPress = function() { performStance(unitIds, data.item); };

		data.button.tooltip = getStanceDisplayName(data.item) + "\n" + bodyFont(getStanceTooltip(data.item));

		data.guiSelection.hidden = !Engine.GuiInterfaceCall("IsStanceSelected", {
			"ents": unitIds,
			"stance": data.item
		});
		data.icon.sprite = "stretched:session/icons/stances/" + data.item + ".png";
		data.button.enabled = controlsPlayer(data.player);

		setPanelObjectPosition(data.button, data.i, data.rowLength);
		return true;
	}
};

g_SelectionPanels.Training = {
	"getMaxNumberOfItems": function()
	{
		return 40 - getNumberOfRightPanelButtons();
	},
	"rowLength": 10,
	"getItems": function()
	{
		return getAllTrainableEntitiesFromSelection();
	},
	"setupButton": function(data)
	{
		const template = GetTemplateData(data.item, data.player);
		if (!template)
			return false;

		const requirementsMet = Engine.GuiInterfaceCall("AreRequirementsMet", {
			"requirements": template.requirements,
			"player": data.player
		});

		const unitIds = data.unitEntStates.map(status => status.id);
		const [buildingsCountToTrainFullBatch, fullBatchSize, remainderBatch] =
			getTrainingStatus(unitIds, data.item, data.playerState);

		const trainNum = buildingsCountToTrainFullBatch * fullBatchSize + remainderBatch;

		let neededResources;
		if (template.cost)
			neededResources = Engine.GuiInterfaceCall("GetNeededResources", {
				"cost": multiplyEntityCosts(template, trainNum),
				"player": data.player
			});

		data.button.onPress = function()
		{
			addTrainingToQueue(unitIds, data.item, data.playerState);
		};

		const showTemplateFunc = () => { showTemplateDetails(data.item, data.playerState.civ); };
		data.button.onPressRight = showTemplateFunc;
		data.button.onPressRightDisabled = showTemplateFunc;

		data.countDisplay.caption = trainNum > 1 ? trainNum : "";

		let tooltips = [
			"[font=\"sans-bold-16\"]" +
				colorizeHotkey("%(hotkey)s", "session.queueunit." + (data.i + 1)) +
				"[/font]" + " " + getEntityNamesFormatted(template),
			getVisibleEntityClassesFormatted(template),
			getAurasTooltip(template),
			getEntityTooltip(template),
			getEntityCostTooltip(template, data.player, unitIds[0], buildingsCountToTrainFullBatch, fullBatchSize, remainderBatch)
		];
		const limits = getEntityLimitAndCount(data.playerState, data.item);
		tooltips.push(formatLimitString(limits.entLimit, limits.entCount, limits.entLimitChangers),
			formatMatchLimitString(limits.matchLimit, limits.matchCount, limits.type));

		if (Engine.ConfigDB_GetValue("user", "showdetailedtooltips") === "true")
			tooltips = tooltips.concat([
				getHealthTooltip,
				getAttackTooltip,
				getHealerTooltip,
				getResistanceTooltip,
				getGarrisonTooltip,
				getTurretsTooltip,
				getProjectilesTooltip,
				getSpeedTooltip,
				getResourceDropsiteTooltip
			].map(func => func(template)));

		tooltips.push(getTemplateViewerOnRightClickTooltip());
		tooltips.push(
			formatBatchTrainingString(buildingsCountToTrainFullBatch, fullBatchSize, remainderBatch),
			getRequirementsTooltip(requirementsMet, template.requirements, GetSimState().players[data.player].civ),
			getNeededResourcesTooltip(neededResources));

		data.button.tooltip = tooltips.filter(tip => tip).join("\n");

		let modifier = "";
		if (!requirementsMet || limits.canBeAddedCount == 0)
		{
			data.button.enabled = false;
			modifier = "color:0 0 0 127:grayscale:";
		}
		else
		{
			data.button.enabled = controlsPlayer(data.player);
			if (neededResources)
				modifier = resourcesToAlphaMask(neededResources) + ":";
		}

		if (data.unitEntStates.every(state => state.upgrade && state.upgrade.isUpgrading))
		{
			data.button.enabled = false;
			modifier = "color:0 0 0 127:grayscale:";
			data.button.tooltip += "\n" + objectionFont(translate("Cannot train while upgrading."));
		}

		if (template.icon)
			data.icon.sprite = modifier + "stretched:session/portraits/" + template.icon;

		const index = data.i + getNumberOfRightPanelButtons();
		setPanelObjectPosition(data.button, index, data.rowLength);

		return true;
	}
};

g_SelectionPanels.Upgrade = {
	"getMaxNumberOfItems": function()
	{
		return 40 - getNumberOfRightPanelButtons();
	},
	"rowLength": 10,
	"getItems": function(unitEntStates)
	{
		// Interface becomes complicated with multiple different units and this is meant per-entity, so prevent it if the selection has multiple different units.
		if (unitEntStates.some(state => state.template != unitEntStates[0].template))
			return false;

		return unitEntStates[0].upgrade && unitEntStates[0].upgrade.upgrades;
	},
	"setupButton": function(data)
	{
		const template = GetTemplateData(data.item.entity);
		if (!template)
			return false;

		const progressOverlay = Engine.GetGUIObjectByName("unitUpgradeProgressSlider[" + data.i + "]");
		progressOverlay.hidden = true;

		const requirementsMet = !data.item.requirements ||
			Engine.GuiInterfaceCall("AreRequirementsMet", {
				"requirements": data.item.requirements,
				"player": data.player
			});

		const limits = getEntityLimitAndCount(data.playerState, data.item.entity);
		const upgradingEntStates = data.unitEntStates.filter(state => state.upgrade.template == data.item.entity);

		const upgradableEntStates = data.unitEntStates.filter(state =>
			!state.upgrade.progress &&
			(!state.production || !state.production.queue || !state.production.queue.length));

		const neededResources = data.item.cost && Engine.GuiInterfaceCall("GetNeededResources", {
			"cost": multiplyEntityCosts(data.item, upgradableEntStates.length),
			"player": data.player
		});

		let tooltip;
		let modifier = "";
		if (!upgradingEntStates.length && upgradableEntStates.length)
		{
			const primaryName = g_SpecificNamesPrimary ? template.name.specific : template.name.generic;
			let secondaryName;
			if (g_ShowSecondaryNames)
				secondaryName = g_SpecificNamesPrimary ? template.name.generic : template.name.specific;

			const tooltips = [];
			if (g_ShowSecondaryNames)
			{
				if (data.item.tooltip)
					tooltips.push(sprintf(translate("Upgrade to a %(primaryName)s (%(secondaryName)s). %(tooltip)s"), {
						"primaryName": primaryName,
						"secondaryName": secondaryName,
						"tooltip": translate(data.item.tooltip)
					}));
				else
					tooltips.push(sprintf(translate("Upgrade to a %(primaryName)s (%(secondaryName)s)."), {
						"primaryName": primaryName,
						"secondaryName": secondaryName
					}));
			}
			else
			{
				if (data.item.tooltip)
					tooltips.push(sprintf(translate("Upgrade to a %(primaryName)s. %(tooltip)s"), {
						"primaryName": primaryName,
						"tooltip": translate(data.item.tooltip)
					}));
				else
					tooltips.push(sprintf(translate("Upgrade to a %(primaryName)s."), {
						"primaryName": primaryName
					}));
			}

			tooltips.push(
				getEntityCostTooltip(data.item, undefined, undefined, data.unitEntStates.length),
				formatLimitString(limits.entLimit, limits.entCount, limits.entLimitChangers),
				formatMatchLimitString(limits.matchLimit, limits.matchCount, limits.type),
				getRequirementsTooltip(requirementsMet, data.item.requirements, GetSimState().players[data.player].civ),
				getNeededResourcesTooltip(neededResources),
				getTemplateViewerOnRightClickTooltip()
			);

			tooltip = tooltips.filter(tip => tip).join("\n");

			data.button.onPress = function()
			{
				upgradeEntity(
					data.item.entity,
					upgradableEntStates.map(state => state.id));
			};

			if (!requirementsMet || limits.canBeAddedCount == 0 &&
				!upgradableEntStates.some(state => hasSameRestrictionCategory(data.item.entity, state.template)))
			{
				data.button.enabled = false;
				modifier = "color:0 0 0 127:grayscale:";
			}
			else if (neededResources)
			{
				data.button.enabled = false;
				modifier = resourcesToAlphaMask(neededResources) + ":";
			}

			data.countDisplay.caption = upgradableEntStates.length > 1 ? upgradableEntStates.length : "";
		}
		else if (upgradingEntStates.length)
		{
			tooltip = translate("Cancel Upgrading");
			data.button.onPress = function() { cancelUpgradeEntity(); };
			data.countDisplay.caption = upgradingEntStates.length > 1 ? upgradingEntStates.length : "";

			let progress = 0;
			for (const state of upgradingEntStates)
				progress = Math.max(progress, state.upgrade.progress || 1);

			// TODO This is bad: we assume the progressOverlay is square
			progressOverlay.size.top = progressOverlay.size.bottom + Math.round((1 - progress) * (progressOverlay.size.left - progressOverlay.size.right));
			progressOverlay.hidden = false;
		}
		else
		{
			tooltip = objectionFont(translatePlural(
				"Cannot upgrade when the entity is training, researching or already upgrading.",
				"Cannot upgrade when all entities are training, researching or already upgrading.",
				data.unitEntStates.length));

			data.button.onPress = function() {};

			data.button.enabled = false;
			modifier = "color:0 0 0 127:grayscale:";
		}
		data.button.enabled = controlsPlayer(data.player);
		data.button.tooltip = tooltip;

		const showTemplateFunc = () => { showTemplateDetails(data.item.entity, data.playerState.civ); };
		data.button.onPressRight = showTemplateFunc;
		data.button.onPressRightDisabled = showTemplateFunc;

		data.icon.sprite = modifier + "stretched:session/" +
			(data.item.icon || "portraits/" + template.icon);

		setPanelObjectPosition(data.button, data.i + getNumberOfRightPanelButtons(), data.rowLength);
		return true;
	}
};

function initSelectionPanels()
{
	const unitBarterPanel = Engine.GetGUIObjectByName("unitBarterPanel");
	if (BarterButtonManager.IsAvailable(unitBarterPanel))
		g_SelectionPanelBarterButtonManager = new BarterButtonManager(unitBarterPanel);

	for (const panel in g_SelectionPanels)
		g_SelectionPanels[panel].init?.();
}

/**
 * Pauses game and opens the template details viewer for a selected entity or technology.
 *
 * Technologies don't have a set civ, so we pass along the native civ of
 * the template of the entity that's researching it.
 *
 * @param {string} [civCode] - The template name of the entity that researches the selected technology.
 */
async function showTemplateDetails(templateName, civCode)
{
	if (inputState != INPUT_NORMAL)
		return;
	g_PauseControl.implicitPause();

	await Engine.OpenChildPage(
		"page_viewer.xml",
		{
			"templateName": templateName,
			"civ": civCode
		});
	resumeGame();
}

/**
 * If two panels need the same space, so they collide,
 * the one appearing first in the order is rendered.
 *
 * Note that the panel needs to appear in the list to get rendered.
 */
const g_PanelsOrder = [
	// LEFT PANE
	"Barter", // Must always be visible on markets
	"Garrison", // More important than Formation, as you want to see the garrisoned units in ships
	"Alert",
	"Formation",
	"Stance", // Normal together with formation

	// RIGHT PANE
	"Gate", // Must always be shown on gates
	"Pack", // Must always be shown on packable entities
	"Upgrade", // Must always be shown on upgradable entities
	"Training",
	"Construction",
	"Research", // Normal together with training

	// UNIQUE PANES (importance doesn't matter)
	"Command",
	"Queue",
	"Selection",
];
