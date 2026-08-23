/**
 * This component builds on the cinema manager component by providing additional structure and wrappers for convenience.
 * It serves as a helper for trigger scripts.
 *
 * Note: Generally, trigger scripts are supposed to only add one speech sequence (list of speech segments) at once. But
 * they don't have to guarantee it: This component is designed to be able to handle multiple at once too, so trigger
 * scripts don't have to perform their own collision prevention logic.
 */
function CinemaInterface() {}

CinemaInterface.prototype.Schema =
	"<a:component type='system'/><empty/>";

CinemaInterface.prototype.Init = function()
{
	this.activeSpeechSequences = [];
	this.temporaryPathCount = 0;
	this.queueEndedCallbacks = [];

	// Players who have already hit the close button of the last active speech sequence.
	this.playersFinishedSpeechSequences = new Set();
};

CinemaInterface.prototype.OnInitGame = function()
{
	this.numHumanPlayers = 0;
	for (const data of InitAttributes.settings?.PlayerData || [{}]) // There's no player data in Atlas.
		if (data && !data.AI && !data.Removed && !data.Unassigned)
			this.numHumanPlayers++;
};

CinemaInterface.prototype.Serialize = function()
{
	const state = {};
	for (const key in this)
		if (Object.hasOwn(this, key))
			state[key] = this[key];

	// Speech sequences without fixed line durations are always restarted from the beginning when deserializing.
	// So this info is irrelevant.
	delete state.playersFinishedSpeechSequences;
	delete state.numHumanPlayers; // Can and must be re-read from InitAttributes when deserializing.

	return state;
};

CinemaInterface.prototype.Deserialize = function(state)
{
	for (const prop in state)
		this[prop] = state[prop];

	this.playersFinishedSpeechSequences = new Set();
	this.OnInitGame();
};


/**
 * Play a cinema path and register a callback that is invoked when the cinema queue finishes.
 * The cinema path is required to be already registered with the CinemaManager component beforehand.
 * If any cinema paths are already being played, the new path is appended to the queue.
 * @param {string[]} pathName - The name of the path to play.
 * @param {Object} callback - Callback data specifying a method on the trigger component and optionally arguments to
 *  pass to it. It is invoked when the all paths have been finished and the game returns to usual gameplay.
 * @param {string} callback.action,
 * @param {*} [callback.args]
 */
CinemaInterface.prototype.PlayExistingPath = function(pathName, callback = null)
{
	const cmpCinemaManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaManager);
	if (!cmpCinemaManager)
		return;

	if (!cmpCinemaManager.IsPlayingQueue())
		cmpCinemaManager.ClearQueue();
	cmpCinemaManager.PushPathToQueue(pathName);
	if (!cmpCinemaManager.IsPlayingQueue())
		cmpCinemaManager.StartPlayingQueue();

	if (callback)
		this.queueEndedCallbacks.push(callback);
};

/**
 * Play a custom cinema path and register a callback that is invoked when the cinema queue finishes.
 * The path being "custom" here means that it isn't already registered with the CinemaManager component and will instead
 * be constructed on the go from the data provided.
 * If any cinema paths are already being played, the new path is appended to the queue.
 * @param {Object} path
 * @param {string} [path.name] - The name of the path. If defined, the path won't be deleted after finishing, so it can
 *  be played again in the future (by name).
 * @param {string} path.orientation - If "target", the property "targetNodes" can and must be specified.
 * @param {Object[]} path.positionNodes - The path along which the camera position is animated.
 * @param {number} paths.positionNodes[].deltaTime - How long the node is active.
 * @param {Vector3D} paths.positionNodes[].position
 * @param {Object[]} path.targetNodes - The path along which the camera orientation is animated.
 * @param {number} paths.targetNodes[].deltaTime - How long the node is active.
 * @param {Vector3D} paths.targetNodes[].position
 * @param {number} [path.timescale] - A factor, by which to multiply the delta times, therefore the path's speed.
 * @param {string} [path.mode] - The distortion mode: "ease_in", "ease_out", "ease_inout" (default), or "ease_outin".
 * @param {string} [path.style] - The distortion style: "default", "expo", "growth", "circle", or "sine".
 * @param {Object} callback - Callback data specifying a method on the trigger component and optionally arguments to
 *  pass to it. It is invoked when the all paths have been finished and the game returns to usual gameplay.
 * @param {string} callback.action
 * @param {*} [callback.args]
 */
CinemaInterface.prototype.PlayCustomPath = function(path, callback = null)
{
	const cmpCinemaManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaManager);
	if (!cmpCinemaManager)
		return;

	if (!path.name)
		// We simply assume that this name is available.
		path.name = "_temporaryPath" + this.temporaryPathCount++;

	cmpCinemaManager.AddPath(path);
	this.PlayExistingPath(path.name, callback);
};

/**
 * Initiate a speech sequence and and display it to all players. It is comprised of one or more speech segments plus
 * some additional options.
 * @param {Object[]} speechSegments
 * @param {Object} speechSegments[] - Data and parameters of a speech segment. If neither lineDurations nor duringPaths
 *  are defined, the GUI shows 'next' and 'previous' buttons to click through the lines manually.
 * @param {number} [speechSegments[].speakingUnit] - The entity ID of the unit to be shown as speaking the lines.
 * @param {string[]} speechSegments[].lines - The spoken/narrated text.
 * @param {number[]} [speechSegments[].lineDurations] - The durations of the lines in milliseconds.
 * @param {string[][]} [speechSegments[].duringPaths] - List of cinema path names for each line, exclusively
 *  during which the line is supposed to be displayed. If defined, but a different path is played, it is
 * cancelled/skipped.
 * @param {Object} [options]
 * @param {boolean} [options.moveCameraToSpeakingUnit] - If true, the camera always jumps to (and follows) the speaking
 *  unit.
 * @param {boolean} [options.enableCutsceneMode] - If true, enables cutscene rendering mode as long as this sequence is
 *  shown. This hides all informational overlays such as territory borders, selection rings, and range overlays, which
 *  makes the scene feel more "pure", more like a movie.
 * @param {boolean} [options.closeSynchronously] - If true or not defined, the cinematic speech overlay only hides once
 *  all players have hit close, and for everyone simultaneously. If false, it can be closed directly. Only relevant in
 *  multiplayer and if the sequence has 'next' and 'previous' buttons (i.e. has no lineDurations or duringPath).
 */
CinemaInterface.prototype.ShowSpeechSequence = function(speechSegments, options)
{
	if (!Array.isArray(speechSegments))
		throw new TypeError("'speechSegments' must be an array.");

	for (const segment of speechSegments)
		if (!Array.isArray(segment.lines))
			throw new TypeError("Each 'speechSegment' must define a 'lines' array.");
		else if (segment.lineDurations && (!Array.isArray(segment.lineDurations) || typeof segment.lineDurations[0] != "number" ||
			segment.lineDurations.length != segment.lines.length)
		)
			throw new Error("When defined, the 'lineDurations' of each speech segment must be an array of numbers and have the same length as its 'lines'.");
		else if (segment.duringPaths && (!Array.isArray(segment.duringPaths) || typeof segment.duringPaths[0] != "string"))
			throw new Error("When defined, the 'duringPaths' of each speech segment must be an array of strings.");

	this.activeSpeechSequences.push({
		"speechSegments": speechSegments,
		"options": options
	});

	this.PushSpeechSequenceToGui(speechSegments, options);
};

CinemaInterface.prototype.PushSpeechSequenceToGui = function(speechSegments, options)
{
	Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface)?.PushNotification({
		"type": "speech-sequence",
		"players": [0],
		"moveCameraToSpeakingUnit": options?.moveCameraToSpeakingUnit,
		"enableCutsceneMode": options?.enableCutsceneMode,
		"closeSynchronously": (options?.closeSynchronously === undefined || options.closeSynchronously) &&
			this.numHumanPlayers > 1,
		"speechSegments": speechSegments
	});
};


/**
 * Mark a player as having finished (clicked through) all of the active speech sequences and wanting to close it now.
 * @param {number} player - The ID of the player who sent the close request.
 */
CinemaInterface.prototype.HandleSpeechSequenceCloseRequest = function(player)
{
	this.playersFinishedSpeechSequences.add(player);
	if (this.playersFinishedSpeechSequences.size < this.numHumanPlayers)
		return;

	// All players have sent a close request, notify the GUI to close the speech sequences (for all players at once) and reset.

	Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface)?.PushNotification({
		"type": "speech-sequence",
		"players": [0],
		"sequenceEnd": true
	});

	this.activeSpeechSequences = [];
	this.playersFinishedSpeechSequences = new Set();
};


CinemaInterface.prototype.OnCinemaQueueEnded = function()
{
	const cmpTrigger = Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger);
	if (!cmpTrigger)
		return;

	const cmpCinemaManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaManager);
	if (cmpCinemaManager)
		while (this.temporaryPathCount > 0)
			cmpCinemaManager.DeletePath("_temporaryPath" + --this.temporaryPathCount);

	for (const callback of this.queueEndedCallbacks)
		if (callback.action in cmpTrigger)
			cmpTrigger[callback.action](callback.args);
		else
			error("CinemaInterface: Failed to call callback  " + uneval(callback) + ". Specified trigger action doesn't exist.");

	this.queueEndedCallbacks = [];
};

CinemaInterface.prototype.OnGlobalDeserialized = function()
{
	const cmpCinemaManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaManager);
	if (!cmpCinemaManager?.IsPlayingQueue())
	{
		for (const sequence of this.activeSpeechSequences)
			this.PushSpeechSequenceToGui(sequence.speechSegments, sequence.options);
		return;
	}

	// The following doesn't affect the serialized simulation state in any way, it's purely done for the GUI's sake.
	// The goal is to find which line was shown at the moment of serializing, also how long it has left, and then reenter
	// the sequences at that point. This is done because their timings could be carefully synced with cinema paths.
	// Note: This isn't 100% foolproof, but also quite an edge case: Players aren't supposed to save and/or quit while
	// a cinema path is playing, so this case should only occur when a player rejoins a multiplayer game while a cinema
	// path is playing, which is a very specific situation. So any more effort than this would be total overkill.

	const activePath = cmpCinemaManager.GetActivePath();
	let timeToSkip = cmpCinemaManager.GetActivePathElapsedTime();
	let done = false;
	const pendingSequences = clone(this.activeSpeechSequences);
	while (pendingSequences.length)
	{
		const segments = pendingSequences[0].speechSegments;
		while (segments.length)
		{
			const firstSegment = segments[0];
			if (firstSegment.duringPaths && !firstSegment.duringPaths.includes(activePath))
			{
				segments.shift();
				continue;
			}

			if (!firstSegment.lineDurations)
			{
				done = true;
				break;
			}

			while (firstSegment.lineDurations.length)
			{
				if (timeToSkip >= firstSegment.lineDurations[0])
				{
					timeToSkip -= firstSegment.lineDurations[0];
					firstSegment.lines.shift();
					firstSegment.lineDurations.shift();
				}
				else
				{
					firstSegment.lineDurations[0] -= timeToSkip;
					done = true;
					break;
				}
			}

			segments.shift();
		}
		if (done)
			break;
		else
			pendingSequences.shift();
	}

	for (const sequence of pendingSequences)
		this.PushSpeechSequenceToGui(sequence.speechSegments, sequence.options);
};


Engine.RegisterSystemComponentType(IID_CinemaInterface, "CinemaInterface", CinemaInterface);
