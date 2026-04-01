/**
 * This component builds on the cinema manager component by providing additional structure and wrappers for convenience.
 * It serves as a helper for trigger scripts.
 */
function CinemaInterface() {}

CinemaInterface.prototype.Schema =
	"<a:component type='system'/><empty/>";

CinemaInterface.prototype.Init = function()
{
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
 * @param {string} [path.name] - The name of the path.
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

	cmpCinemaManager.AddPath(path);
	this.PlayExistingPath(path.name, callback);
};

CinemaInterface.prototype.OnCinemaQueueEnded = function()
{
	const cmpTrigger = Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger);
	if (!cmpTrigger)
		return;

	for (const callback of this.queueEndedCallbacks)
		if (callback.action in cmpTrigger)
			cmpTrigger[callback.action](callback.args);
		else
			error("CinemaInterface: Failed to call callback  " + uneval(callback) + ". Specified trigger action doesn't exist.");

	this.queueEndedCallbacks = [];
};

Engine.RegisterSystemComponentType(IID_CinemaInterface, "CinemaInterface", CinemaInterface);
