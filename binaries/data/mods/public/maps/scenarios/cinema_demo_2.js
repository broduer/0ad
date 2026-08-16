Trigger.prototype.PlayFirstCutscene = function()
{
	// This path is already registered under that name, it was drawn in atlas and is stored in the map's XML.
	const firstCinemaPathName = "path1";

	// This is a custom path not yet registered anywhere, we define it entirely here.
	const secondCinemaPathData = {
		"timescale": 1,
		"orientation": "target",
		"mode": "ease_inout",
		"style": "default",
		"positionNodes": [
			{
				"position": new Vector3D(290.31104, 49.29522, 400.7906),
				"deltaTime": 0
			},
			{
				"position": new Vector3D(254.64576, 66.57055, 352.07383),
				"deltaTime": 4
			},
			{
				"position": new Vector3D(188.43287, 85.68216, 318.43021),
				"deltaTime": 4
			},
			{
				"position": new Vector3D(187.1648, 84.94903, 315.23832),
				"deltaTime": 4
			}
		],
		"targetNodes": [
			{
				"position": new Vector3D(264.95313, 46.68482, 365.76944),
				"deltaTime": 0
			},
			{
				"position": new Vector3D(238.60918, 56.95272, 311.4221),
				"deltaTime": 4
			},
			{
				"position": new Vector3D(234.26212, 58.81025, 301.79963),
				"deltaTime": 4
			},
			{
				"position": new Vector3D(235.16155, 58.12488, 299.04053),
				"deltaTime": 4
			}
		]
	};

	const cmpCinemaInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaInterface);
	if (cmpCinemaInterface)
		cmpCinemaInterface.PlayExistingPath(firstCinemaPathName, { "action": "PlaySecondCutscene", "args": secondCinemaPathData });
};

Trigger.prototype.PlaySecondCutscene = function(pathData)
{
	const cmpCinemaInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaInterface);
	cmpCinemaInterface.PlayCustomPath(pathData);
};

Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger).RegisterTrigger("OnInitGame", "PlayFirstCutscene", { "enabled": true });
