// The number of currently visible buttons (used to optimise showing/hiding)
var g_unitPanelButtons = {
	"Selection": 0,
	"Queue": 0,
	"Formation": 0,
	"Garrison": 0,
	"Training": 0,
	"Research": 0,
	"Alert": 0,
	"Barter": 0,
	"Construction": 0,
	"Command": 0,
	"Stance": 0,
	"Gate": 0,
	"Pack": 0,
	"Upgrade": 0
};

/**
 * Set the position of a panel object according to the index,
 * from left to right, from top to bottom.
 * Will wrap around to subsequent rows if the index
 * is larger than rowLength.
 */
function setPanelObjectPosition(object, index, rowLength, vMargin = 1, hMargin = 1)
{
	const oWidth = object.size.right - object.size.left;
	const oHeight = object.size.bottom - object.size.top;
	const left = (index % rowLength) * (oWidth + vMargin);
	const top = (Math.floor(index / rowLength)) * (oHeight + hMargin);

	Object.assign(object.size, {
		// horizontal position
		"left": left,
		"right": left + oWidth,
		// vertical position
		"top": top,
		"bottom": top + oHeight
	});
}

/**
 * Helper function for updateUnitCommands; sets up "unit panels"
 * (i.e. panels with rows of icons) for the currently selected unit.
 *
 * @param guiName Short identifier string of this panel. See g_SelectionPanels.
 * @param unitEntStates Entity states of the selected units
 * @param playerState Player state
 */
function setupUnitPanel(guiName, unitEntStates, playerState)
{
	if (!g_SelectionPanels[guiName])
	{
		error("unknown guiName used '" + guiName + "'");
		return;
	}

	const items = g_SelectionPanels[guiName].getItems(unitEntStates);

	if (!items || !items.length)
		return;

	const numberOfItems = Math.min(items.length, g_SelectionPanels[guiName].getMaxNumberOfItems());
	const rowLength = g_SelectionPanels[guiName].rowLength || 8;

	if (g_SelectionPanels[guiName].resizePanel)
		g_SelectionPanels[guiName].resizePanel(numberOfItems, rowLength);

	for (let i = 0; i < numberOfItems; ++i)
	{
		const data = {
			"i": i,
			"item": items[i],
			"playerState": playerState,
			"player": unitEntStates[0].player,
			"unitEntStates": unitEntStates,
			"rowLength": rowLength,
			"numberOfItems": numberOfItems,
			// depending on the XML, some of the GUI objects may be undefined
			"button": Engine.TryGetGUIObjectByName("unit" + guiName + "Button[" + i + "]"),
			"icon": Engine.TryGetGUIObjectByName("unit" + guiName + "Icon[" + i + "]"),
			"guiSelection": Engine.TryGetGUIObjectByName("unit" + guiName + "Selection[" + i + "]"),
			"countDisplay": Engine.TryGetGUIObjectByName("unit" + guiName + "Count[" + i + "]")
		};

		if (data.button)
		{
			data.button.hidden = false;
			data.button.enabled = true;
			data.button.tooltip = "";
			data.button.caption = "";
		}

		if (g_SelectionPanels[guiName].setupButton &&
		    !g_SelectionPanels[guiName].setupButton(data))
			continue;

		// TODO: we should require all entities to have icons, so this case never occurs
		if (data.icon && !data.icon.sprite)
			data.icon.sprite = "BackgroundBlack";
	}

	// Hide any buttons we're no longer using
	for (let i = numberOfItems; i < g_unitPanelButtons[guiName]; ++i)
		if (g_SelectionPanels[guiName].hideItem)
			g_SelectionPanels[guiName].hideItem(i, rowLength);
		else
			Engine.GetGUIObjectByName("unit" + guiName + "Button[" + i + "]").hidden = true;

	g_unitPanelButtons[guiName] = numberOfItems;
	g_SelectionPanels[guiName].used = true;
}

/**
 * Updates the selection panels where buttons are supposed to
 * depend on the context.
 * Runs in the main session loop via updateSelectionDetails().
 * Delegates to setupUnitPanel to set up individual subpanels,
 * appropriately activated depending on the selected unit's state.
 *
 * @param entStates Entity states of the selected units
 * @param supplementalDetailsPanel Reference to the
 *        "supplementalSelectionDetails" GUI Object
 * @param commandsPanel Reference to the "commandsPanel" GUI Object
 */
function updateUnitCommands(entStates, supplementalDetailsPanel, commandsPanel)
{
	for (const panel in g_SelectionPanels)
		g_SelectionPanels[panel].used = false;

	// Get player state to check some constraints
	// e.g. presence of a hero or build limits.
	const playerStates = GetSimState().players;
	const playerState = playerStates[Engine.GetPlayerID()];

	if (g_IsObserver || entStates.every(entState =>
		controlsPlayer(entState.player) &&
		(!entState.identity || entState.identity.controllable)) ||
		playerState.controlsAll)
	{
		for (const guiName of g_PanelsOrder)
		{
			if (g_SelectionPanels[guiName].conflictsWith &&
			    g_SelectionPanels[guiName].conflictsWith.some(p => g_SelectionPanels[p].used))
				continue;

			setupUnitPanel(guiName, entStates, playerStates[entStates[0].player]);
		}

		supplementalDetailsPanel.hidden = false;
		commandsPanel.hidden = false;
	}
	else
	{
		// Always show what entities are selected, no matter if they can be controlled.
		setupUnitPanel("Selection", entStates, playerStates[entStates[0].player]);
		// Always show the commands since they might not require the entities to be owned.
		// TODO: This panel here is NOT related to the commandsPanel GUI object. The naming should be improved.
		setupUnitPanel("Command", entStates, playerState);

		if (playerState.isMutualAlly[entStates[0].player])
		{
			// TODO if there's a second panel needed for a different player
			// we should consider adding the players list to g_SelectionPanels
			setupUnitPanel("Garrison", entStates, playerState);

			supplementalDetailsPanel.hidden = !g_SelectionPanels.Garrison.used;

			commandsPanel.hidden = true;
		}
		else
		{
			supplementalDetailsPanel.hidden = true;
			commandsPanel.hidden = true;
		}
	}

	// Hides / unhides Unit Panels (panels should be grouped by type, not by order, but we will leave that for another time)
	for (const panelName in g_SelectionPanels)
		Engine.GetGUIObjectByName("unit" + panelName + "Panel").hidden = !g_SelectionPanels[panelName].used;
}

// Force hide commands panels
function hideUnitCommands()
{
	for (var panelName in g_SelectionPanels)
		Engine.GetGUIObjectByName("unit" + panelName + "Panel").hidden = true;
}

// Get all trainable items per player from the selection
function getAllTrainableItems(selection)
{
	const items = [];
	const seen = new Set(); // key: player:template

	for (const ent of selection)
	{
		const state = GetEntityState(ent);
		if (!state?.trainer?.entities?.length)
			continue;

		for (const tmpl of state.trainer.entities)
		{
			const key = state.player + ":" + tmpl;
			if (!seen.has(key))
			{
				seen.add(key);
				items.push({
					"template": tmpl,
					"player": state.player,
					"trainers": [ent]
				});
			}
			else
			{
				// Add to existing item's trainers list
				const existing = items.find(item => item.player === state.player && item.template === tmpl);
				if (existing && !existing.trainers.includes(ent))
					existing.trainers.push(ent);
			}
		}
	}
	return items;
}

function getAllTrainableItemsFromSelection()
{
	if (!g_allTrainableEntities)
		g_allTrainableEntities = getAllTrainableItems(g_Selection.toList());

	return g_allTrainableEntities;
}

// Get all researchable items from the selection, grouped by researchers entities.
function getAllResearchItems(selection)
{
	const items = [];
	const itemMap = new Map(); // key -> item index

	for (const ent of selection)
	{
		const state = GetEntityState(ent);
		if (!state?.researcher?.technologies?.length)
			continue;

		if (!state.production)
			warn("Researcher without ProductionQueue found: " + ent + ".");

		const player = state.player;
		const facilityId = ent;
		const isUpgrading = !!state.upgrade && state.upgrade.isUpgrading;

		for (const tech of state.researcher.technologies)
		{
			let key;
			if (tech && tech.pair)
				key = player + ":pair:" + tech.bottom + "|" + tech.top;
			else
				key = player + ":" + tech;

			if (itemMap.has(key))
			{
				const idx = itemMap.get(key);
				const item = items[idx];

				// Add this facility to the item's facilities list
				if (!item.researchFacilityIds.includes(facilityId))
					item.researchFacilityIds.push(facilityId);

				if (!isUpgrading)
					item.hasAvailableFacility = true;
			}
			else
			{
				const item = {
					"tech": tech,
					"techCostMultiplier": state.researcher.techCostMultiplier,
					"researchFacilityIds": [facilityId],
					"player": player,
					"hasAvailableFacility": !isUpgrading // At least this facility is available
				};
				itemMap.set(key, items.length);
				items.push(item);
			}
		}
	}

	// Sort items to prioritize those with available facilities
	items.sort((a, b) => (b.hasAvailableFacility ? 1 : 0) - (a.hasAvailableFacility ? 1 : 0));

	return items;
}

function getAllResearchItemsFromSelection()
{
	// TODO : consider implementing a cache like g_allResearchItems
	return getAllResearchItems(g_Selection.toList());
}

// Get all upgrade items from the selection, grouped by player and upgrade template
function getAllUpgradeItems(selection)
{
	const items = [];
	const seen = new Map(); // key: player:upgradeTemplate -> item index

	for (const ent of selection)
	{
		const state = GetEntityState(ent);
		if (!state?.upgrade?.upgrades)
			continue;

		const player = state.player;
		for (const upgrade of state.upgrade.upgrades)
		{
			const key = player + ":" + upgrade.entity;
			if (seen.has(key))
			{
				const idx = seen.get(key);
				items[idx].entities.push(ent);
			}
			else
			{
				seen.set(key, items.length);
				items.push({
					"upgradeTemplate": upgrade.entity,
					"player": player,
					"entities": [ent],
					"upgradeData": upgrade
				});
			}
		}
	}
	return items;
}

function getAllUpgradeItemsFromSelection()
{
	// TODO : consider implementing a cache like g_allUpgradeItems
	return getAllUpgradeItems(g_Selection.toList());
}

// Get all buildable items per player from the selection
function getAllBuildableItems(selection)
{
	const items = [];
	const seen = new Set(); // key: player:template

	// Group entities by player
	const byPlayer = new Map();
	for (const ent of selection)
	{
		const state = GetEntityState(ent);
		if (!state) continue;
		const player = state.player;
		if (!byPlayer.has(player))
			byPlayer.set(player, []);
		byPlayer.get(player).push(ent);
	}

	// For each player, get their buildable templates
	for (const [player, entities] of byPlayer)
	{
		const templates = Engine.GuiInterfaceCall("GetAllBuildableEntities", { "entities": entities });
		for (const tmpl of templates)
		{
			const key = player + ":" + tmpl;
			if (!seen.has(key))
			{
				seen.add(key);
				items.push({ "template": tmpl, "player": player });
			}
		}
	}
	return items;
}

function getAllBuildableItemsFromSelection()
{
	if (!g_allBuildableEntities)
		g_allBuildableEntities = getAllBuildableItems(g_Selection.toList());
	return g_allBuildableEntities;
}

function getNumberOfRightPanelButtons()
{
	var sum = 0;

	for (const prop of ["Construction", "Training", "Pack", "Gate", "Upgrade"])
		if (g_SelectionPanels[prop].used)
			sum += g_unitPanelButtons[prop];

	return sum;
}
