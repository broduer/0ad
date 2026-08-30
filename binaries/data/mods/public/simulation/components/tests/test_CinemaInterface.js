Engine.LoadComponentScript("interfaces/CinemaInterface.js");
Engine.LoadComponentScript("interfaces/PlayerManager.js");
Engine.LoadComponentScript("interfaces/Trigger.js");
Engine.LoadComponentScript("interfaces/UnitAI.js");
Engine.LoadComponentScript("CinemaInterface.js");

let cmpCinemaInterface;

let cinemaPathQueue;
let isPlayingQueue;
let addedPaths;
let deletedPaths;
let activePath;

let callbacksMade;

const Reset = () =>
{
	cmpCinemaInterface = ConstructComponent(SYSTEM_ENTITY, "CinemaInterface");

	cinemaPathQueue = ["otherPath0", "otherPath1"];
	isPlayingQueue = false;
	addedPaths = [];
	deletedPaths = [];
	activePath = "";

	callbacksMade = [
		{ "count": 0, "lastArgs": null },
		{ "count": 0, "lastArgs": null },
		{ "count": 0, "lastArgs": null },
		{ "count": 0, "lastArgs": null }
	];
};

AddMock(SYSTEM_ENTITY, IID_CinemaManager, {
	"AddPath": (path) => { addedPaths.push(path); },
	"DeletePath": (path) => { deletedPaths.push(path); },
	"ClearQueue": () => { cinemaPathQueue = []; },
	"PushPathToQueue": (path) => { cinemaPathQueue.push(path); },
	"StartPlayingQueue": () => { isPlayingQueue = true; },
	"IsPlayingQueue": () => isPlayingQueue,
	"GetActivePath": () => activePath,
	"GetActivePathElapsedTime": () => 10000
});

AddMock(SYSTEM_ENTITY, IID_Trigger, {
	"CallbackMethod0": (args) =>
	{
		callbacksMade[0].count++;
		callbacksMade[0].lastArgs = args;
	},
	"CallbackMethod1": (args) =>
	{
		callbacksMade[1].count++;
		callbacksMade[1].lastArgs = args;
	},
	"CallbackMethod2": (args) =>
	{
		callbacksMade[2].count++;
		callbacksMade[2].lastArgs = args;
	},
	"CallbackMethod3": (args) =>
	{
		callbacksMade[3].count++;
		callbacksMade[3].lastArgs = args;
	}
});

Reset();

(function TestPlayingCinemaPaths()
{
	cmpCinemaInterface.PlayExistingPath("path1");
	TS_ASSERT_UNEVAL_EQUALS(cinemaPathQueue, ["path1"]);
	TS_ASSERT(isPlayingQueue);

	isPlayingQueue = false;
	const callback0 = { "action": "CallbackMethod0", "args": { "A": 1, "B": 2 } };
	const callback1 = { "action": "CallbackMethod1", "args": { "A": 2, "B": 2 } };
	const customPathData = {
		"timescale": 1,
		"orientation": "target",
		"mode": "ease_inout",
		"style": "default",
		"positionNodes": [
			{ "deltaTime": 0, "position": new Vector3D(10, 20, 30) },
			{ "deltaTime": 0, "position": new Vector3D(100, 1000, 100) }
		],
		"targetNodes": [
			{ "deltaTime": 0, "position": new Vector3D(10.5, 10.9, 10.5) },
			{ "deltaTime": 0, "position": new Vector3D(0, 13, 90) }
		]
	};
	cmpCinemaInterface.PlayExistingPath("path2", callback0);
	cmpCinemaInterface.PlayExistingPath("path3");
	const path4Data = { "name": "path4", ...customPathData };
	cmpCinemaInterface.PlayCustomPath(path4Data);
	cmpCinemaInterface.PlayCustomPath(customPathData, callback1);
	TS_ASSERT_UNEVAL_EQUALS(addedPaths, [path4Data, { ...customPathData, "name": "temporaryPath0" }]);
	TS_ASSERT_UNEVAL_EQUALS(cinemaPathQueue, ["path2", "path3", "path4", "temporaryPath0"]);
	TS_ASSERT_EQUALS(cmpCinemaInterface.temporaryPathCount, 1);
	TS_ASSERT(isPlayingQueue);
	TS_ASSERT_EQUALS(callbacksMade[0].count, 0);
	TS_ASSERT_EQUALS(callbacksMade[1].count, 0);

	cinemaPathQueue = [];
	isPlayingQueue = false;
	cmpCinemaInterface.OnCinemaQueueEnded();
	TS_ASSERT_UNEVAL_EQUALS(deletedPaths, ["temporaryPath0"]);
	TS_ASSERT_EQUALS(callbacksMade[0].count, 1);
	TS_ASSERT_UNEVAL_EQUALS(callbacksMade[0].lastArgs, callback0.args);
	TS_ASSERT_EQUALS(callbacksMade[1].count, 1);
	TS_ASSERT_UNEVAL_EQUALS(callbacksMade[1].lastArgs, callback1.args);
})();

Reset();

(function TestShowingSpeechSequences()
{
	AddMock(SYSTEM_ENTITY, IID_PlayerManager, {
		"GetHumanPlayers": () => [1, 2]
	});
	AddMock(SYSTEM_ENTITY, IID_GuiInterface, {
		"PushNotification": () => {}
	});

	const sequence1 = [
		{
			"duringPaths": ["path0"],
			"speakingUnit": 41,
			"lines": ["an epic monologue"],
			"lineDurations": [20000],
		},
		{
			"speakingUnit": 42,
			"lines": ["a super cool response", "another monologue that's somehow even more epic than the first one"]
		}
	];
	const options1 = {
		"closeSynchronously": true,
		"moveCameraToSpeakingUnit": false,
		"enableCutsceneMode": false
	};
	const options2 = {
		"closeSynchronously": false,
		"moveCameraToSpeakingUnit": true,
		"enableCutsceneMode": true
	};
	const callback1_conflict = { "action": "CallbackMethod1", "args": 10 };
	const callback1 = { "action": "CallbackMethod1", "args": 11 };
	const callback2 = { "action": "CallbackMethod2", "args": ["test"] };
	cmpCinemaInterface.PlayExistingPath("path0", callback1_conflict);
	cmpCinemaInterface.ShowSpeechSequence(sequence1, options1, callback1);
	TS_ASSERT_UNEVAL_EQUALS(cmpCinemaInterface.activeSpeechSequences, [{
		"speechSegments": sequence1,
		"options": options1
	}]);

	TS_ASSERT(isPlayingQueue);

	const customPathData = {
		"name": "path4",
		"timescale": 0.7,
		"mode": "ease_outin",
		"style": "sine",
		"positionNodes": [
			{ "deltaTime": 0, "position": new Vector3D(5, 10, 5) },
			{ "deltaTime": 0, "position": new Vector3D(23, 12, 13) }
		]
	};
	const sequence2 = [
		{
			"paths": ["path1", "path2", "path3"],
			"lines": ["first line", "second line", "third line", "fourth line"],
		},
		{
			"paths": [customPathData, "path5"],
			"lines": ["Not the last line.", "Not the first line."],
			"lineDurations": [1300, 1700]
		}
	];
	const speechSegments2 = clone(sequence2);
	speechSegments2[0].duringPaths = speechSegments2[0].paths;
	delete speechSegments2[0].paths;
	speechSegments2[1].duringPaths = [customPathData.name, speechSegments2[1].paths[1]];
	delete speechSegments2[1].paths;

	cmpCinemaInterface.PlayPathsWithSpeechSequence(sequence2, options2, callback2);
	TS_ASSERT_UNEVAL_EQUALS(cinemaPathQueue, ["path0", "path1", "path2", "path3", "path4", "path5"]);
	TS_ASSERT_UNEVAL_EQUALS(cmpCinemaInterface.activeSpeechSequences, [
		{
			"speechSegments": sequence1,
			"options": options1
		},
		{
			"speechSegments": speechSegments2,
			"options": options2
		}
	]);
	TS_ASSERT_UNEVAL_EQUALS(cmpCinemaInterface.triggerCallbacks, [callback1_conflict, callback1, callback2]);


	// Test whether callbacks are invoked at the right time and in the right order.

	TS_ASSERT_UNEVAL_EQUALS(Array.from(cmpCinemaInterface.playersFinishedSpeechSequences), []);
	cmpCinemaInterface.HandleSpeechSequenceCloseRequest(1);
	TS_ASSERT_UNEVAL_EQUALS(Array.from(cmpCinemaInterface.playersFinishedSpeechSequences), [1]);
	cmpCinemaInterface.HandleSpeechSequenceCloseRequest(1);
	TS_ASSERT_UNEVAL_EQUALS(Array.from(cmpCinemaInterface.playersFinishedSpeechSequences), [1]);

	// Speech sequence isn't over yet, so callbacks mustn't be invoked yet.
	cmpCinemaInterface.OnCinemaQueueEnded();
	isPlayingQueue = false;
	TS_ASSERT_EQUALS(callbacksMade[1].count, 0);
	TS_ASSERT_EQUALS(callbacksMade[2].count, 0);

	cmpCinemaInterface.HandleSpeechSequenceCloseRequest(2);
	TS_ASSERT_UNEVAL_EQUALS(Array.from(cmpCinemaInterface.playersFinishedSpeechSequences), []);
	TS_ASSERT_EQUALS(callbacksMade[1].count, 2);
	TS_ASSERT_EQUALS(callbacksMade[1].lastArgs, callback1.args);
	TS_ASSERT_EQUALS(callbacksMade[2].count, 1);
	TS_ASSERT_EQUALS(callbacksMade[2].lastArgs, callback2.args);
	TS_ASSERT_UNEVAL_EQUALS(cmpCinemaInterface.activeSpeechSequences, []);


	const callback3 = { "action": "CallbackMethod3", "args": {} };
	cmpCinemaInterface.ShowSpeechSequence(sequence1, options1, callback3);
	isPlayingQueue = true;

	cmpCinemaInterface.HandleSpeechSequenceCloseRequest(1);
	// Cinema paths are still playing, so callbacks mustn't be invoked yet.
	cmpCinemaInterface.HandleSpeechSequenceCloseRequest(2);
	TS_ASSERT_EQUALS(callbacksMade[3].count, 0);

	cmpCinemaInterface.OnCinemaQueueEnded();
	TS_ASSERT_EQUALS(callbacksMade[3].count, 1);
	TS_ASSERT_EQUALS(callbacksMade[3].lastArgs, callback3.args);
})();
