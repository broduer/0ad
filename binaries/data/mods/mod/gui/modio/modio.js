/**
 * modiomod-r28 — gui/modio/modio.js
 * Revision: 1.2.3 (internal rev 4)
 * Last revision: 2026-09-10 22:10 CET
 *
 * Change notes:
 * - rev4 (2026-09-10, Claude on behalf of Grautvornix): Added an on-screen colour
 *   legend (see new function updateColorLegend(), called once from init()),
 *   at Grautvornix's request, so the meaning of the gray/red/yellow/green/white
 *   row colours doesn't have to be memorised. This required shipping this
 *   mod's own gui/modio/modio.xml for the first time (previously only
 *   modio.js was overridden) to add the new "modioColorLegend" text object —
 *   there is no scripting API in 0 A.D. to create GUI objects at runtime
 *   (confirmed against source/gui/Scripting/JSInterface_GUIManager.cpp), so a
 *   new visible element can only come from XML. The new object was placed in
 *   previously-empty space (between the mod filter box and the "Filter valid
 *   mods" panel) without moving or resizing any existing object. Trade-off
 *   worth flagging: this mod now shadows modio.xml as well as modio.js, so a
 *   future 0 A.D. release that changes this page's layout will no longer be
 *   picked up automatically — it will need re-diffing against the new stock
 *   modio.xml the way modio.js already is.
 * - rev3 (2026-09-10, Claude on behalf of Grautvornix): mod.json "name" changed
 *   from "modiomod" to "modiomod-r28" and "label" to "ModIO downloader (R28)",
 *   at Grautvornix's request, to disambiguate this build (targeting the actual
 *   0.28 classic-script API, see rev2 below) from the parallel
 *   "modiomod-r29" build (targeting the ES-module API seen in 0.29-dev trunk)
 *   when both are kept installed side by side for testing against different
 *   game versions. Both previously shared the internal mod name "modiomod",
 *   which risked ambiguous behaviour if both were ever enabled at once, on
 *   top of the two builds looking identical in the "Modifications"/mod
 *   selector list. No functional change to this file.
 * - rev2 (2026-09-10, Claude on behalf of Grautvornix): Corrected against the
 *   actual shipped 0.28 sources (binaries/data/mods/mod/mod.zip from
 *   C:\Spiele\0AD-28.0, extracted by Grautvornix to "v. 0.28\gui"). rev1 had
 *   wrongly assumed, from a 0.29.0-dev SVN trunk checkout, that 0.28 already
 *   loads this file as an ES module (<script module="..."/>, requiring
 *   `export function init()`). In-game testing of rev1 threw a hard parse
 *   error: "export declarations may only appear at top level of a module" —
 *   proof the engine was loading it as a classic script after all. 0.28's own
 *   binaries/data/mods/mod/gui/modio/modio.xml still declares
 *   <script directory="gui/modio/"/> (not module="..."), and still wires
 *   modFilter/modsAvailableList/compatibilityFilter/refreshButton/
 *   downloadButton events via inline XML <action on="Press">...</action>
 *   attributes rather than JS. So this revision: (a) dropped the `export`
 *   keyword rev1 had added to `init`, and (b) removed the JS-side
 *   Object.assign/onPress event-wiring block rev1 had added inside init()
 *   (that block belongs to trunk/future versions only; on 0.28 it did nothing
 *   useful and only risked overriding the XML-registered handlers). Only
 *   backButton's onPress and the "modio" object's onTick are wired in JS —
 *   that part was already correct, since 0.28's modio.xml has no <action> for
 *   backButton either. The rest of rev1 stands verified against 0.28's actual
 *   modio.js: the async/Promise-based dialog and mod.io state-machine flow
 *   (messageBox() via Engine.OpenChildPage() resolving to a button index,
 *   init() returning Promise.race(...) so the page closes when that promise
 *   resolves) is unchanged in 0.28 from what rev1 already had, confirmed
 *   line-for-line against binaries/data/mods/mod/gui/modio/modio.js as
 *   shipped in 0.28.
 * - rev1 (2026-09-10, Claude on behalf of Grautvornix): Root cause found: 0.26's
 *   modio.js used the pre-Release-28 callback-driven messageBox()/dialog
 *   convention and (per this file's actual 0.28 behaviour) needed no
 *   structural change there, but did need updating for the same async
 *   dialog/state-machine flow 0.28 introduced — see rev2 for the corrected,
 *   verified version. Restored this mod's own value-add, which stock 0.28's
 *   simplified modio.js dropped: green/yellow/red/gray colour coding by
 *   owned/update-available/dependencies-met/invalid, and the dependency- and
 *   update-warning text in the mod description panel. That logic
 *   (versionUpdate, isUpdateAvailable, areDependenciesMet, isDependencyMet,
 *   versionSatisfied, isOwnedMod, getModIOColor, colorModIO,
 *   g_RegExpComparisonOperator) is unchanged from the 0.26 revision.
 *   mod.json "dependencies" corrected to "0ad>=0.0.28", since the async
 *   dialog/state-machine flow this revision now matches is a real 0.28
 *   requirement and this file will not behave correctly on 0.26/0.27 (the
 *   mod.json dependency string itself, contrary to Grautvornix's first attempt at
 *   a fix, was never the actual problem — the script's own outdated control
 *   flow was).
 */

let g_ModsAvailableOnline = [];
/**
 * Mod definitions loaded from the files, including invalid mods.
 */
let g_Mods = {};
/**
 * Indicates if we have encountered an error in one of the network-interaction attempts.
 *
 * We use a global so we don't get multiple messageBoxes appearing (one for each "tick").
 *
 * Set to `true` by showErrorMessageBox
 * Set to `false` by init, updateModList, downloadFile, and cancelRequest
 */
let g_Failure;

/**
 * Indicates if the user has cancelled a request.
 *
 * Primarily used so the user can cancel the mod list fetch, as whenever that get cancelled,
 * the modio state reverts to "ready", even if we've successfully listed mods before.
 *
 * Set to `true` by cancelRequest
 * Set to `false` by updateModList, and downloadFile
 */
let g_RequestCancelled;

let g_RequestStartTime;

const g_ModIOState = {
	/**
	 * Finished status indicators
	 */
	"ready": (progressData, closePageCallback) =>
	{
		// GameID acquired, ready to fetch mod list
		if (!g_RequestCancelled)
			updateModList(closePageCallback);
	},
	"listed": progressData =>
	{
		// List of available mods acquired

		// Only run this once (for each update).
		if (Engine.GetGUIObjectByName("modsAvailableList").list.length)
			return;

		hideDialog();
		Engine.GetGUIObjectByName("refreshButton").enabled = true;
		g_ModsAvailableOnline = Engine.ModIoGetMods();
		g_Mods = Engine.GetAvailableMods();
		displayMods();
	},
	"success": progressData =>
	{
		// Successfully acquired a mod file
		hideDialog();
		Engine.GetGUIObjectByName("downloadButton").enabled = true;
		g_Mods = Engine.GetAvailableMods();
		displayMods();
	},
	/**
	 * In-progress status indicators.
	 */
	"gameid": progressData =>
	{
		// Acquiring GameID from mod.io
	},
	"listing": progressData =>
	{
		// Acquiring list of available mods from mod.io
	},
	"downloading": progressData =>
	{
		// Downloading a mod file
		updateProgressBar(progressData.progress, g_ModsAvailableOnline[selectedModIndex()].filesize);
	},
	/**
	 * Error/Failure status indicators.
	 */
	"failed_gameid": async(progressData, closePageCallback) =>
	{
		// Game ID couldn't be retrieved
		const promise = showErrorMessageBox(
			sprintf(translateWithContext("mod.io error message", "Game ID could not be retrieved.\n\n%(technicalDetails)s"), {
				"technicalDetails": progressData.error
			}),
			translateWithContext("mod.io error message", "Initialization Error"),
			[translate("Abort"), translate("Retry")]);
		if (!promise)
			return;
		if (await promise === 0)
			closePageCallback();
		else
			init();
	},
	"failed_listing": async(progressData, closePageCallback) =>
	{
		// Mod list couldn't be retrieved
		const promise = showErrorMessageBox(
			sprintf(translateWithContext("mod.io error message", "Mod List could not be retrieved.\n\n%(technicalDetails)s"), {
				"technicalDetails": progressData.error
			}),
			translateWithContext("mod.io error message", "Fetch Error"),
			[translate("Abort"), translate("Retry")]);
		if (!promise)
			return;
		if (await promise === 0)
			cancelModListUpdate(closePageCallback);
		else
			updateModList(closePageCallback);
	},
	"failed_downloading": async(progressData) =>
	{
		// File couldn't be retrieved
		const promise = showErrorMessageBox(
			sprintf(translateWithContext("mod.io error message", "File download failed.\n\n%(technicalDetails)s"), {
				"technicalDetails": progressData.error
			}),
			translateWithContext("mod.io error message", "Download Error"),
			[translate("Abort"), translate("Retry")]);
		if (!promise)
			return;
		if (await promise === 0)
			cancelRequest();
		else
			downloadMod();
	},
	"failed_filecheck": async(progressData) =>
	{
		// The file is corrupted
		const promise = showErrorMessageBox(
			sprintf(translateWithContext("mod.io error message", "File verification error.\n\n%(technicalDetails)s"), {
				"technicalDetails": progressData.error
			}),
			translateWithContext("mod.io error message", "Verification Error"),
			[translate("Abort")]);
		if (!promise)
			return;
		await promise;
		cancelRequest();
	},
	/**
	 * Default
	 */
	"none": progressData =>
	{
		// Nothing has happened yet.
	}
};

function init(data)
{
	const promise = progressDialog(
		translate("Initializing mod.io interface."),
		translate("Initializing"),
		false,
		translate("Cancel"));

	g_Failure = false;
	Engine.ModIoStartGetGameId();

	updateColorLegend();

	return Promise.race([
		promise,
		new Promise(closePageCallback =>
		{
			Engine.GetGUIObjectByName("backButton").onPress = closePageCallback;
			Engine.GetGUIObjectByName("modio").onTick = onTick.bind(null, closePageCallback);
		})
	]);
}

function onTick(closePageCallback)
{
	const progressData = Engine.ModIoGetDownloadProgress();

	const handler = g_ModIOState[progressData.status];
	if (!handler)
	{
		warn("Unrecognized progress status: " + progressData.status);
		return;
	}

	handler(progressData, closePageCallback);
	if (!progressData.status.startsWith("failed"))
		Engine.ModIoAdvanceRequest();
}

function displayMods()
{
	const modsAvailableList = Engine.GetGUIObjectByName("modsAvailableList");
	const selectedMod = modsAvailableList.list[modsAvailableList.selected];
	modsAvailableList.selected = -1;

	let displayedMods = clone(g_ModsAvailableOnline);
	for (let i = 0; i < displayedMods.length; ++i)
		displayedMods[i].i = i;

	const filterColumns = ["name", "name_id", "summary"];
	const filterText = Engine.GetGUIObjectByName("modFilter").caption.toLowerCase();
	if (Engine.GetGUIObjectByName("compatibilityFilter").checked)
		displayedMods = displayedMods.filter(mod => !mod.invalid);
	displayedMods = displayedMods.filter(mod => filterColumns.some(column => mod[column].toLowerCase().indexOf(filterText) != -1));

	displayedMods.sort((mod1, mod2) =>
		modsAvailableList.selected_column_order *
		(modsAvailableList.selected_column == "filesize" ?
			mod1.filesize - mod2.filesize :
			String(mod1[modsAvailableList.selected_column]).localeCompare(String(mod2[modsAvailableList.selected_column]))));

	const updateAvailable = displayedMods.map(mod => isUpdateAvailable(mod.name_id, mod.version));
	const dependenciesMet = displayedMods.map(mod => areDependenciesMet(mod.dependencies));
	const updateIds = displayedMods.map(mod => mod.i);
	modsAvailableList.list_name = displayedMods.map(mod => colorModIO(mod.name, !mod.invalid, updateAvailable[updateIds.indexOf(mod.i)], isOwnedMod(mod.name_id), dependenciesMet[updateIds.indexOf(mod.i)]));
	modsAvailableList.list_name_id = displayedMods.map(mod => colorModIO(mod.name_id, !mod.invalid, updateAvailable[updateIds.indexOf(mod.i)], isOwnedMod(mod.name_id), dependenciesMet[updateIds.indexOf(mod.i)]));
	modsAvailableList.list_version = displayedMods.map(mod => colorModIO(mod.version || "", !mod.invalid, updateAvailable[updateIds.indexOf(mod.i)], isOwnedMod(mod.name_id), dependenciesMet[updateIds.indexOf(mod.i)]));
	modsAvailableList.list_filesize = displayedMods.map(mod => colorModIO(filesizeToString(mod.filesize), !mod.invalid, updateAvailable[updateIds.indexOf(mod.i)], isOwnedMod(mod.name_id), dependenciesMet[updateIds.indexOf(mod.i)]));
	modsAvailableList.list_dependencies = displayedMods.map(mod => colorModIO((mod.dependencies || []).join(" "), !mod.invalid, updateAvailable[updateIds.indexOf(mod.i)], isOwnedMod(mod.name_id), dependenciesMet[updateIds.indexOf(mod.i)]));
	modsAvailableList.list = displayedMods.map(mod => mod.i);
	modsAvailableList.selected = modsAvailableList.list.indexOf(selectedMod);
}

function versionUpdate(version1, version2)
{
	let versionList1 = version1.split(/[-_]/)[0].split(/\./g);
	let versionList2 = version2.split(/[-_]/)[0].split(/\./g);

	for (let i = 0; i < Math.min(versionList1.length, versionList2.length); ++i)
	{
		let diff = +versionList1[i] - +versionList2[i];
		if (diff > 0)
			return true;
		if (diff < 0)
			return false;
	}

	// common prefix matches
	let ldiff = versionList1.length - versionList2.length;
	return ldiff && ldiff >= 0;
}

function isUpdateAvailable(name, version)
{
	const mod = g_Mods[name];
	if (!!mod && !!mod.version)
		return versionUpdate(version, mod.version);
	return false;
}

function areDependenciesMet(dependencies)
{
	if (!!dependencies)	{
		for (let dependency of dependencies) {
			if (!isDependencyMet(dependency))
				return false;
		}
	}
	return true;
}

/**
 * @param dependency is a mod name or a mod version comparison.
 */
function isDependencyMet(dependency)
{
	let operator = dependency.match(g_RegExpComparisonOperator);
	let [name, version] = operator ? dependency.split(operator[0]) : [dependency, undefined];

	for (const i in g_Mods) {
		const mod = g_Mods[i];
		if (mod.name == name && (!operator || versionSatisfied(mod.version, operator[0], version)))
			return true;
	}
	return false;
}

/**
 * Compares the given versions using the given operator.
 *       '-' or '_' is ignored. Only numbers are supported.
 * @note "5.3" < "5.3.0"
 */
function versionSatisfied(version1, operator, version2)
{
	let versionList1 = version1.split(/[-_]/)[0].split(/\./g);
	let versionList2 = version2.split(/[-_]/)[0].split(/\./g);

	let eq = operator.indexOf("=") != -1;
	let lt = operator.indexOf("<") != -1;
	let gt = operator.indexOf(">") != -1;

	for (let i = 0; i < Math.min(versionList1.length, versionList2.length); ++i)
	{
		let diff = +versionList1[i] - +versionList2[i];

		if (gt && diff > 0 || lt && diff < 0)
			return true;

		if (gt && diff < 0 || lt && diff > 0 || eq && diff)
			return false;
	}

	// common prefix matches
	let ldiff = versionList1.length - versionList2.length;
	if (!ldiff)
		return eq;

	// NB: 2.3 != 2.3.0
	if (ldiff < 0)
		return lt;

	return gt;
}

function isOwnedMod(name)
{
	return !!g_Mods && !!g_Mods[name];
}
const g_RegExpComparisonOperator = /(<=|>=|<|>|=)/;
function getModIOColor(valid, update, owned, dependencies)
{
	return !valid ? "gray" : !dependencies ? "red" : update ? "yellow" : owned ? "green" :  false;
}

function colorModIO(text, valid, update, owned, dependencies)
{
	const color = getModIOColor(valid, update, owned, dependencies);
	return color ? coloredText(text, color) : text;
}

/**
 * Static on-screen legend explaining the row colours used by colorModIO()/
 * getModIOColor(). Set once, since it doesn't depend on the mod list.
 * The "modioColorLegend" object is only present in this mod's own
 * gui/modio/modio.xml (added alongside this function), so this is
 * defensive against running with a stock modio.xml that lacks it.
 */
function updateColorLegend()
{
	const legend = Engine.GetGUIObjectByName("modioColorLegend");
	if (!legend)
		return;

	legend.caption =
		translate("Legend:") + "  " +
		coloredText(translate("invalid"), "gray") + "   " +
		coloredText(translate("dependency missing"), "red") + "   " +
		coloredText(translate("update available"), "yellow") + "   " +
		coloredText(translate("installed, up to date"), "green") + "   " +
		translate("not installed (white)");
}

function clearModList()
{
	const modsAvailableList = Engine.GetGUIObjectByName("modsAvailableList");
	modsAvailableList.selected = -1;
	modsAvailableList.list_name = [];
	modsAvailableList.list_name_id = [];
	modsAvailableList.list_version = [];
	modsAvailableList.list_filesize = [];
	modsAvailableList.list_dependencies = [];
	modsAvailableList.list = [];
}

function selectedModIndex()
{
	const modsAvailableList = Engine.GetGUIObjectByName("modsAvailableList");

	if (modsAvailableList.selected == -1)
		return undefined;

	return +modsAvailableList.list[modsAvailableList.selected];
}

function isSelectedModInvalid(selected)
{
	return selected !== undefined && !!g_ModsAvailableOnline[selected].invalid && g_ModsAvailableOnline[selected].invalid == "true";
}

function showModDescription()
{
	const selected = selectedModIndex();
	const isSelected = selected !== undefined;
	const isInvalid = isSelectedModInvalid(selected);
	let warningString = "";
	let infoString = "";
	if (isSelected && !isInvalid) {
		const mod = g_ModsAvailableOnline[selected];
		const isUpdate = isUpdateAvailable(mod.name_id, mod.version);
		const isDep = areDependenciesMet(mod.dependencies);
		if (!isDep) {
			warningString = coloredText("(Warning: " + "Dependencies might not be met." + ") ", "yellow");
		}
		if (isUpdate) {
			infoString  = coloredText("(Info: " + "Update is available" + ") ", "yellow");
		}
	}
	Engine.GetGUIObjectByName("downloadButton").enabled = isSelected && !isInvalid;
	Engine.GetGUIObjectByName("modDescription").caption = isSelected && !isInvalid ? warningString + infoString + g_ModsAvailableOnline[selected].summary : "";
	Engine.GetGUIObjectByName("modError").caption = isSelected && isInvalid ? sprintf(translate("Invalid mod: %(error)s"), {"error": g_ModsAvailableOnline[selected].error }) : "";
}

function cancelModListUpdate(closePageCallback)
{
	cancelRequest();

	if (!g_ModsAvailableOnline.length)
	{
		closePageCallback();
		return;
	}

	displayMods();
	Engine.GetGUIObjectByName('refreshButton').enabled = true;
}

async function updateModList(closePageCallback)
{
	clearModList();
	g_Mods = Engine.GetAvailableMods();
	Engine.GetGUIObjectByName("refreshButton").enabled = false;

	const promise = progressDialog(
		translate("Fetching and updating list of available mods."),
		translate("Updating"),
		false,
		translate("Cancel Update"));

	g_Failure = false;
	g_RequestCancelled = false;
	Engine.ModIoStartListMods();

	await promise;
	cancelModListUpdate(closePageCallback);
}

async function downloadMod()
{
	const selected = selectedModIndex();

	if (isSelectedModInvalid(selected))
		return;

	const promise = progressDialog(
		sprintf(translate("Downloading “%(modname)s”"), {
			"modname": g_ModsAvailableOnline[selected].name
		}),
		translate("Downloading"),
		true,
		translate("Cancel Download"));

	Engine.GetGUIObjectByName("downloadButton").enabled = false;

	g_Failure = false;
	g_RequestCancelled = false;
	Engine.ModIoStartDownloadMod(selected);

	await promise;
	Engine.GetGUIObjectByName("downloadButton").enabled = true;
}

function cancelRequest()
{
	g_Failure = false;
	g_RequestCancelled = true;
	Engine.ModIoCancelRequest();
	hideDialog();
}

function showErrorMessageBox(caption, title, buttonCaptions)
{
	if (g_Failure)
		return null;
	g_Failure = true;

	return messageBox(500, 250, caption, title, buttonCaptions);
}

async function progressDialog(dialogCaption, dialogTitle, showProgressBar, buttonCaption)
{
	Engine.GetGUIObjectByName("downloadDialog_title").caption = dialogTitle;

	const downloadDialogCaption = Engine.GetGUIObjectByName("downloadDialog_caption");
	downloadDialogCaption.caption = dialogCaption;
	downloadDialogCaption.size.rbottom = showProgressBar ? 40 : 80;

	Engine.GetGUIObjectByName("downloadDialog_progress").hidden = !showProgressBar;
	Engine.GetGUIObjectByName("downloadDialog_status").hidden = !showProgressBar;

	Engine.GetGUIObjectByName("downloadDialog").hidden = false;

	g_RequestStartTime = Date.now();

	const downloadDialog_button = Engine.GetGUIObjectByName("downloadDialog_button");
	downloadDialog_button.caption = buttonCaption;
	await new Promise(resolve =>
	{
		downloadDialog_button.onPress = resolve;
	});
	cancelRequest();
}

/*
 * The "remaining time" and "average speed" texts both naively assume that
 * the connection remains relatively stable throughout the download.
 */
function updateProgressBar(progress, totalSize)
{
	const progressPercent = Math.ceil(progress * 100);
	Engine.GetGUIObjectByName("downloadDialog_progressBar").progress = progressPercent;

	const transferredSize = progress * totalSize;
	const transferredSizeObj = filesizeToObj(transferredSize);
	// Translation: Mod file download indicator. Current size over expected final size, with percentage complete.
	Engine.GetGUIObjectByName("downloadDialog_progressText").caption = sprintf(translate("%(current)s / %(total)s (%(percent)s%%)"), {
		"current": filesizeToObj(totalSize).unit == transferredSizeObj.unit ? transferredSizeObj.filesize : filesizeToString(transferredSize),
		"total": filesizeToString(totalSize),
		"percent": progressPercent
	});

	const elapsedTime = Date.now() - g_RequestStartTime;
	const remainingTime = progressPercent ? (100 - progressPercent) * elapsedTime / progressPercent : 0;
	const avgSpeed = filesizeToObj(transferredSize / (elapsedTime / 1000));
	// Translation: Mod file download status message.
	Engine.GetGUIObjectByName("downloadDialog_status").caption = sprintf(translate("Time Elapsed: %(elapsed)s\nEstimated Time Remaining: %(remaining)s\nAverage Speed: %(avgSpeed)s"), {
		"elapsed": timeToString(elapsedTime),
		"remaining": remainingTime ? timeToString(remainingTime) : translate("∞"),
		// Translation: Average download speed, used to give the user a very rough and naive idea of the download time. For example: 123.4 KiB/s
		"avgSpeed": sprintf(translate("%(number)s %(unit)s/s"), {
			"number": avgSpeed.filesize,
			"unit": avgSpeed.unit
		})
	});
}

function hideDialog()
{
	Engine.GetGUIObjectByName("downloadDialog").hidden = true;
}
