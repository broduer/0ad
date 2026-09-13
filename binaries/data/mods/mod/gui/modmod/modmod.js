import { downloadModsButton } from "gui/modmod/modmodio.js";
import { regExpComparisonOperator, validateMod } from "gui/modmod/validatemod.js";

/**
 * Mod definitions loaded from the files, including invalid mods.
 */
let g_Mods = {};

/**
 * Folder names of all mods that are or can be launched.
 */
let g_ModsEnabled = [];
let g_ModsDisabled = [];

let g_ModsEnabledFiltered = [];
let g_ModsDisabledFiltered = [];

/**
 * Cache mod compatibility recomputed when some mod is enbaled/disabled.
 */
const g_ModsCompatibility = [];

/**
 * Name of the mods installed by the ModInstaller.
 */
let g_InstalledMods;

let g_HasIncompatibleMods;

/**
 * Mods listed on mod.io, cached after checkForUpdates() completes.
 * Not populated on this page's initial load, so isModUpdateAvailable()
 * correctly returns false until the player explicitly checks.
 */
let g_ModsAvailableOnline = [];

/* eslint-disable prefer-const -- Mods should be able to change them */
let g_FakeMod = {
	"name": translate("This mod does not exist"),
	"version": "",
	"label": "",
	"url": "",
	"description": "",
	"dependencies": []
};

let g_ColorNoModSelected = "255 255 100";
let g_ColorDependenciesMet = "100 255 100";
let g_ColorDependenciesNotMet = "255 100 100";
/* eslint-enable prefer-const */

export function init(data, hotloadData)
{
	Object.assign(Engine.GetGUIObjectByName("modsDisabledList"), {
		"onSelectionChange": selectedMod.bind(undefined, "modsDisabledList"),
		"onSelectionColumnChange": displayModLists,
		"onMouseLeftDoubleClickItem": enableMod
	});
	Object.assign(Engine.GetGUIObjectByName("modsEnabledList"), {
		"onSelectionChange": selectedMod.bind(undefined, "modsEnabledList"),
		"onMouseLeftDoubleClickItem": disableMod
	});

	Engine.GetGUIObjectByName("enabledModUp").onPress =
		moveCurrItem.bind(undefined, "modsEnabledList", true);
	Engine.GetGUIObjectByName("enabledModDown").onPress =
		moveCurrItem.bind(undefined, "modsEnabledList", false);

	// Fix for a stock bug: these three used to be wired via inline XML
	// <action on="Press">displayModLists();</action> attributes, which
	// cannot work now that this page loads as an ES module (displayModLists
	// is module-scoped, not a global XML actions can call). Wired here
	// instead, using the same Object.assign()/onPress idiom already used
	// for every other control on this page.
	Object.assign(Engine.GetGUIObjectByName("modGenericFilter"), {
		"onPress": displayModLists,
		"onTextEdit": displayModLists
	});
	Engine.GetGUIObjectByName("negateFilter").onPress = displayModLists;
	Engine.GetGUIObjectByName("modCompatibleFilter").onPress = displayModLists;

	Engine.GetGUIObjectByName("visitWebButton").onPress = visitModWebsite;
	Engine.GetGUIObjectByName("downloadButton").onPress = downloadModsButton.bind(undefined, initMods);
	Engine.GetGUIObjectByName("saveConfigurationButton").onPress = saveMods;
	Engine.GetGUIObjectByName("startButton").onPress = startMods;

	// Check for Updates: added by modiomod, see checkForUpdates() below.
	Engine.GetGUIObjectByName("checkUpdatesButton").onPress = checkForUpdates;

	g_InstalledMods = data && data.installedMods || hotloadData && hotloadData.installedMods || [];
	g_HasIncompatibleMods = Engine.HasIncompatibleMods();

	initMods();
	initGUIButtons(data);
	if (g_HasIncompatibleMods)
		Engine.OpenChildPage("page_incompatible_mods.xml", {});

	return new Promise(closePageCallback =>
	{
		Engine.GetGUIObjectByName("quitButton").onPress = closePageCallback;
		Engine.GetGUIObjectByName("cancelButton").onPress = closePageCallback.bind(undefined, {
			[Engine.openRequest]: { "page": "page_pregame.xml" }
		});
	});
}

function initMods()
{
	loadMods();
	loadEnabledMods();
	recomputeCompatibility();
	validateMods();
	initGUIFilters();
}

function getHotloadData()
{
	return { "installedMods": g_InstalledMods };
}

function loadMods()
{
	g_Mods = Engine.GetAvailableMods();
	deepfreeze(g_Mods);
}

/**
 * Return fake mod for mods which do not exist
*/
function getMod(folder)
{
	return g_Mods[folder] ? g_Mods[folder] : g_FakeMod;
}

function loadEnabledMods()
{
	if (g_HasIncompatibleMods)
		g_ModsEnabled = Engine.GetEnabledMods().concat(Engine.GetIncompatibleMods())
			.filter(folder => folder != "mod");
	else
		g_ModsEnabled = Engine.GetEnabledMods().filter(folder => !!g_Mods[folder]);
	g_ModsDisabled = Object.keys(g_Mods).filter(folder => g_ModsEnabled.indexOf(folder) == -1);
	g_ModsEnabledFiltered = g_ModsEnabled;
	g_ModsDisabledFiltered = g_ModsDisabled;
}

function validateMods()
{
	for (const folder in g_Mods)
		validateMod(folder, g_Mods[folder], true);
}

function initGUIFilters()
{
	Engine.GetGUIObjectByName("negateFilter").checked = false;
	Engine.GetGUIObjectByName("modCompatibleFilter").checked = true;

	displayModLists();
}

function initGUIButtons(data)
{
	// Either get back to the previous page or quit if there is no previous page
	const hasPreviousPage = !data || data.cancelbutton || false;
	Engine.GetGUIObjectByName("cancelButton").hidden = !hasPreviousPage;
	Engine.GetGUIObjectByName("quitButton").hidden = hasPreviousPage;
	// Turn 'save' off, it will be enabled on any change.
	Engine.GetGUIObjectByName("saveConfigurationButton").enabled = false;
	Engine.GetGUIObjectByName("toggleModButton").caption = translateWithContext("mod activation", "Enable");
}

function saveMods()
{
	sortEnabledMods();
	Engine.ConfigDB_CreateAndSaveValue("user", "mod.enabledmods", ["mod"].concat(g_ModsEnabled).join(" "));

	Engine.GetGUIObjectByName("saveConfigurationButton").enabled = false;
}

function startMods()
{
	saveMods();
	if (!Engine.SetModsAndRestartEngine(["mod"].concat(g_ModsEnabled)))
		Engine.GetGUIObjectByName("message").caption = coloredText(translate('Dependencies not met'), g_ColorDependenciesNotMet);
}

function displayModLists()
{
	g_ModsEnabledFiltered = displayModList("modsEnabledList", g_ModsEnabled, true);
	g_ModsDisabledFiltered = displayModList("modsDisabledList", g_ModsDisabled, false);
}

function displayModList(listObjectName, folders, enabled)
{
	const listObject = Engine.GetGUIObjectByName(listObjectName);

	if (listObjectName == "modsDisabledList")
	{
		const sortFolder = folder => String(getMod(folder)[listObject.selected_column] || folder);
		folders.sort((folder1, folder2) =>
			listObject.selected_column_order *
			sortFolder(folder1).localeCompare(sortFolder(folder2)));
	}

	folders = folders.filter(filterMod);
	if (!enabled && Engine.GetGUIObjectByName("modCompatibleFilter").checked)
		folders = folders.filter(folder => g_ModsCompatibility[folder]);

	const selected = listObject.selected !== -1 ? listObject.list_name[listObject.selected] : null;

	listObject.list_name = folders.map(folder => colorMod(folder, getMod(folder).name || "", enabled));
	listObject.list_folder = folders.map(folder => colorMod(folder, folder, enabled));
	listObject.list_label = folders.map(folder => colorMod(folder, getMod(folder).label || "", enabled));
	listObject.list_url = folders.map(folder => colorMod(folder, getMod(folder).url || "", enabled));
	listObject.list_version = folders.map(folder => colorMod(folder, getMod(folder).version || "", enabled));
	listObject.list_dependencies = folders.map(folder => colorMod(folder, getMod(folder)?.dependencies.join(" ") || "", enabled));
	listObject.list = folders;

	listObject.selected = selected ? listObject.list_name.indexOf(selected) : -1;

	return folders;
}

function getModColor(folder, enabled)
{
	if (!g_ModsCompatibility[folder])
		return enabled ? g_ColorDependenciesNotMet : "gray";
	if (isModUpdateAvailable(folder))
		return "yellow";
	if (g_InstalledMods.indexOf(getMod(folder).name) != -1)
		return "green";
	return false;
}

/**
 * @returns {boolean} whether mod.io lists a newer version of the mod
 * installed in `folder` than the one currently on disk. Always false until
 * checkForUpdates() has completed at least once (g_ModsAvailableOnline is
 * empty on page load, by design — this is a checked-on-demand feature, not
 * an automatic one).
 */
function isModUpdateAvailable(folder)
{
	if (!g_ModsAvailableOnline.length)
		return false;

	const localMod = getMod(folder);
	if (!localMod.version)
		return false;

	const onlineMod = g_ModsAvailableOnline.find(mod => mod.name_id == folder && !mod.invalid);
	return !!onlineMod && !!onlineMod.version && versionSatisfied(onlineMod.version, ">", localMod.version);
}

/**
 * Starts a fresh mod.io game-id + mod-list fetch, driven by
 * onModIoUpdateCheckTick() via this page's own onTick, so the "installed
 * mods" list can be recoloured with an "update available" indicator without
 * opening the separate mod.io downloader page.
 */
function checkForUpdates()
{
	Engine.GetGUIObjectByName("checkUpdatesButton").enabled = false;
	Engine.GetGUIObjectByName("message").caption = coloredText(
		translate("Checking mod.io for updates..."), g_ColorNoModSelected);

	Engine.ModIoStartGetGameId();
	Engine.GetGUIObjectByName("modmod").onTick = onModIoUpdateCheckTick;
}

function onModIoUpdateCheckTick()
{
	const progressData = Engine.ModIoGetDownloadProgress();

	if (progressData.status == "ready")
		Engine.ModIoStartListMods();
	else if (progressData.status == "listed")
	{
		g_ModsAvailableOnline = Engine.ModIoGetMods();
		finishUpdateCheck(true);
		return;
	}
	else if (progressData.status.startsWith("failed"))
	{
		finishUpdateCheck(false, progressData.error);
		return;
	}

	Engine.ModIoAdvanceRequest();
}

/**
 * A do-nothing tick handler. finishUpdateCheck() below assigns this instead
 * of null/undefined to detach onModIoUpdateCheckTick once the check is
 * done: in-game testing (0.28, modiomod-r28 1.3.1) showed that leaving
 * "modmod"'s onTick set to null makes the engine log "on-event-handlers
 * must be functions" / "'onTick' is not a function" every single frame
 * from then on, rather than simply not calling it — i.e. once a handler
 * has been assigned, the property must keep holding an actual function
 * afterwards. This mod has no access to the engine's C++ GUI-scripting
 * source to confirm the exact mechanism, so this is a fix grounded
 * directly in that observed behaviour and in the engine's own error text
 * ("must be functions"), not in a verified source reference. Fixed here
 * identically, since this function is unchanged from modiomod-r28's.
 */
function noopTick()
{
}

function finishUpdateCheck(success, error)
{
	Engine.GetGUIObjectByName("modmod").onTick = noopTick;
	Engine.GetGUIObjectByName("checkUpdatesButton").enabled = true;

	if (!success)
	{
		Engine.ModIoCancelRequest();
		Engine.GetGUIObjectByName("message").caption = coloredText(
			sprintf(translate("Could not check mod.io for updates. %(technicalDetails)s"), { "technicalDetails": error }),
			g_ColorDependenciesNotMet);
		return;
	}

	Engine.GetGUIObjectByName("message").caption = coloredText(
		translate("Checked mod.io for updates."), g_ColorDependenciesMet);
	displayModLists();
}

function colorMod(folder, text, enabled)
{
	const color = getModColor(folder, enabled);
	return color ? coloredText(text, color) : text;
}

function reloadDisabledMods()
{
	g_ModsDisabled = Object.keys(g_Mods).filter(folder => g_ModsEnabled.indexOf(folder) == -1);
}

function enableMod()
{
	const modsDisabledList = Engine.GetGUIObjectByName("modsDisabledList");
	let pos = modsDisabledList.selected;

	if (pos == -1 || !g_ModsCompatibility[g_ModsDisabledFiltered[pos]])
		return;

	g_ModsEnabled.push(g_ModsDisabledFiltered.splice(pos, 1)[0]);
	reloadDisabledMods();
	recomputeCompatibility();

	Engine.GetGUIObjectByName("saveConfigurationButton").enabled = true;

	if (pos >= g_ModsDisabledFiltered.length)
		--pos;

	displayModLists();
	Engine.GetGUIObjectByName("message").caption = "";
	modsDisabledList.selected = pos;
}

function disableMod()
{
	const modsEnabledList = Engine.GetGUIObjectByName("modsEnabledList");
	const pos = modsEnabledList.selected;
	if (pos == -1)
		return;

	// Find true position of disabled mod and remove it
	const disabledMod = g_ModsEnabledFiltered[pos];
	for (let i = 0; i < g_ModsEnabled.length; ++i)
		if (g_ModsEnabled[i] == disabledMod)
		{
			g_ModsEnabled.splice(i, 1);
			break;
		}

	if (g_Mods[disabledMod])
		g_ModsDisabled.push(disabledMod);

	// Remove mods that required the removed mod and cascade
	// Sort them, so we know which ones can depend on the removed mod
	// TODO: Find position where the removed mod would have fit (for now assume idx 0)

	sortEnabledMods();

	for (let i = 0; i < g_ModsEnabled.length; ++i)
		if (!areDependenciesMet(g_ModsEnabled[i], true))
		{
			g_ModsDisabled.push(g_ModsEnabled.splice(i, 1)[0]);
			--i;
		}

	Engine.GetGUIObjectByName("saveConfigurationButton").enabled = true;

	recomputeCompatibility(true);
	displayModLists();
	Engine.GetGUIObjectByName("message").caption = "";
	modsEnabledList.selected = Math.min(pos, g_ModsEnabledFiltered.length - 1);
}

function filterMod(folder)
{
	const mod = getMod(folder);

	const negateFilter = Engine.GetGUIObjectByName("negateFilter").checked;
	const searchText = Engine.GetGUIObjectByName("modGenericFilter").caption;

	if (searchText &&
	    folder.indexOf(searchText) == -1 &&
	    (mod.name || "").indexOf(searchText) == -1 &&
	    (mod.label || "").indexOf(searchText) == -1 &&
	    (mod.url || "").indexOf(searchText) == -1 &&
	    (mod.version || "").indexOf(searchText) == -1 &&
	    (mod.description || "").indexOf(searchText) == -1 &&
	    (mod.dependencies || "").indexOf(searchText) == -1)
		return negateFilter;

	return !negateFilter;
}

/**
 * Moves an item in the list up or down.
 */
function moveCurrItem(objectName, up)
{
	// Prevent moving while filters are applied
	// because we would need to map filtered positions
	// to not filtered positions so changes will persist.
	if (Engine.GetGUIObjectByName("modGenericFilter").caption)
		return;

	const obj = Engine.GetGUIObjectByName(objectName);
	const idx = obj.selected;
	if (idx == -1)
		return;

	const num = obj.list.length;
	const idx2 = idx + (up ? -1 : 1);
	if (idx2 < 0 || idx2 >= num)
		return;

	const tmp = g_ModsEnabled[idx];
	g_ModsEnabled[idx] = g_ModsEnabled[idx2];
	g_ModsEnabled[idx2] = tmp;

	g_ModsEnabledFiltered = displayModList("modsEnabledList", g_ModsEnabled, true);
	obj.selected = idx2;
}

function areDependenciesMet(folder, disabledAction = false)
{
	// If we disabled mod it will not change satus of incompatible mods
	if (disabledAction && !g_ModsCompatibility[folder])
		return g_ModsCompatibility[folder];

	if (!g_Mods[folder])
		return false;

	for (const dependency of getMod(folder).dependencies)
	{
		if (!isDependencyMet(dependency))
			return false;
	}

	return true;
}

function recomputeCompatibility(disabledAction = false)
{
	for (const mod in g_Mods)
		g_ModsCompatibility[mod] = areDependenciesMet(mod, disabledAction);
}

/**
 * @param dependency is a mod name or a mod version comparison.
 */
function isDependencyMet(dependency)
{
	const operator = dependency.match(regExpComparisonOperator);
	const [name, version] = operator ? dependency.split(operator[0]) : [dependency, undefined];

	return g_ModsEnabled.some(folder =>
		getMod(folder).name == name &&
		(!operator || versionSatisfied(getMod(folder).version, operator[0], version)));
}

/**
 * Compares the given versions using the given operator.
 *       '-' or '_' is ignored. Only numbers are supported.
 * @note "5.3" < "5.3.0"
 */
function versionSatisfied(version1, operator, version2)
{
	const versionList1 = version1.split(/[-_]/)[0].split(/\./g);
	const versionList2 = version2.split(/[-_]/)[0].split(/\./g);

	const eq = operator.indexOf("=") != -1;
	const lt = operator.indexOf("<") != -1;
	const gt = operator.indexOf(">") != -1;

	for (let i = 0; i < Math.min(versionList1.length, versionList2.length); ++i)
	{
		const diff = +versionList1[i] - +versionList2[i];

		if (gt && diff > 0 || lt && diff < 0)
			return true;

		if (gt && diff < 0 || lt && diff > 0 || eq && diff)
			return false;
	}

	// common prefix matches
	const ldiff = versionList1.length - versionList2.length;
	if (!ldiff)
		return eq;

	// NB: 2.3 != 2.3.0
	if (ldiff < 0)
		return lt;

	return gt;
}

function sortEnabledMods()
{
	const dependencies = {};
	for (const folder of g_ModsEnabled)
		dependencies[folder] = getMod(folder).dependencies.map(d => d.split(regExpComparisonOperator)[0]);

	g_ModsEnabled.sort((folder1, folder2) =>
		dependencies[folder1].indexOf(getMod(folder2).name) != -1 ? 1 :
			dependencies[folder2].indexOf(getMod(folder1).name) != -1 ? -1 : 0);

	g_ModsEnabledFiltered = displayModList("modsEnabledList", g_ModsEnabled, true);
}

function selectedMod(listObjectName)
{
	const listObject = Engine.GetGUIObjectByName(listObjectName);
	const isPickedDisabledList = listObjectName == "modsDisabledList";
	const otherListObject = Engine.GetGUIObjectByName(isPickedDisabledList ?
		"modsEnabledList" : "modsDisabledList");

	const toggleModButton = Engine.GetGUIObjectByName("toggleModButton");
	const isModSelected = listObject.selected != -1;
	if (isModSelected)
	{
		otherListObject.selected = -1;
		toggleModButton.onPress = isPickedDisabledList ? enableMod : disableMod;
	}

	const isFiltering = Engine.GetGUIObjectByName("modGenericFilter").caption;
	Engine.GetGUIObjectByName("visitWebButton").enabled = isModSelected && !!getSelectedModUrl();
	toggleModButton.caption = isPickedDisabledList ?
		translateWithContext("mod activation", "Enable") :
		translateWithContext("mod activation", "Disable");
	toggleModButton.enabled = isPickedDisabledList ? isModSelected && g_ModsCompatibility[listObject.list[listObject.selected]] || false : isModSelected;
	Engine.GetGUIObjectByName("enabledModUp").enabled = isModSelected && listObjectName == "modsEnabledList" && !isFiltering;
	Engine.GetGUIObjectByName("enabledModDown").enabled = isModSelected && listObjectName == "modsEnabledList" && !isFiltering;

	Engine.GetGUIObjectByName("globalModDescription").caption =
		listObject.list[listObject.selected] ?
			getMod(listObject.list[listObject.selected]).description :
			'[color="' + g_ColorNoModSelected + '"]' + translate("No mod has been selected.") + '[/color]';

	if (!g_ModsEnabled.length)
		Engine.GetGUIObjectByName("message").caption = coloredText(translate('Enable at least 0ad mod'), g_ColorDependenciesNotMet);
	if (!Engine.GetGUIObjectByName("startButton").hidden)
		Engine.GetGUIObjectByName("startButton").enabled = g_ModsEnabled.length > 0;

}

/**
 * @returns {string} The url of the currently selected mod.
 */
function getSelectedModUrl()
{
	const modsEnabledList = Engine.GetGUIObjectByName("modsEnabledList");
	const modsDisabledList = Engine.GetGUIObjectByName("modsDisabledList");

	const list = modsEnabledList.selected == -1 ? modsDisabledList : modsEnabledList;
	const folder = list.list[list.selected];
	return folder && getMod(folder) && getMod(folder).url || undefined;
}

function visitModWebsite()
{
	let url = getSelectedModUrl();
	if (!url)
		return;

	if (!url.startsWith("http://") && !url.startsWith("https://"))
		url = "http://" + url;

	openURL(url);
}
