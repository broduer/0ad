/**
 * This component builds on the cinema manager component by providing additional structure and wrappers for convenience.
 * It serves as a helper for trigger scripts.
 */
function CinemaInterface() {}

CinemaInterface.prototype.Schema =
	"<a:component type='system'/><empty/>";

CinemaInterface.prototype.Init = function()
{
	this.temporaryPathCount = 0;
	this.queueEndedCallbacks = [];
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
 */
CinemaInterface.prototype.ShowSpeechSequence = function(speechSegments, options)
{
	if (!Array.isArray(speechSegments))
		throw new TypeError("'speechSegments' must be an array.");

	for (const segment of speechSegments)
		if (!Array.isArray(segment.lines))
			throw new TypeError("Each 'speechSegment' must define a 'lines' array.");
		else if (!Array.isArray(segment.lineDurations) && !Array.isArray(segment.duringPaths))
			throw new TypeError("Each 'speechSegment' must define either a 'lineDurations' array or 'duringPaths' array.");
		else if (segment.lineDurations && segment.lineDurations.length != segment.lines.length)
			throw new Error("When defined, the 'lineDurations' array of each speech segment must have the same length as its 'lines'.");

	Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface)?.PushNotification({
		"type": "speech-sequence",
		"players": [0],
		"moveCameraToSpeakingUnit": options?.moveCameraToSpeakingUnit,
		"enableCutsceneMode": options?.enableCutsceneMode,
		"speechSegments": speechSegments
	});

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

Engine.RegisterSystemComponentType(IID_CinemaInterface, "CinemaInterface", CinemaInterface);
