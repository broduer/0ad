Engine.LoadHelperScript("Player.js");
Engine.LoadComponentScript("interfaces/Formation.js");
Engine.LoadComponentScript("interfaces/Health.js");
Engine.LoadComponentScript("RallyPoint.js");

function initialRallyPointTest(test_function)
{
	ResetState();

	const entityID = 123;
	AddMock(entityID, IID_Ownership, { "GetOwner": () => 1 });
	const cmpRallyPoint = ConstructComponent(entityID, "RallyPoint", {});

	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(), []);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), []);

	cmpRallyPoint.AddPosition(3, 1415);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), [{ "x": 3, "z": 1415 }]);

	cmpRallyPoint.AddPosition(926, 535);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), [{ "x": 3, "z": 1415 }, { "x": 926, "z": 535 }]);

	const targetID = 456;
	const myData = { "command": "write a unit test", "target": targetID };
	cmpRallyPoint.AddData(myData);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), [{ "x": 3, "z": 1415 }, { "x": 926, "z": 535 }]);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(), [myData]);

	const targetID2 = 789;
	const myData2 = { "command": "this time really", "target": targetID2 };
	cmpRallyPoint.AddData(myData2);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(), [myData, myData2]);

	if (test_function(cmpRallyPoint))
	{
		TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(), []);
		TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), []);
	}
	else
	{
		TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(), [myData, myData2]);
		TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), [{ "x": 3, "z": 1415 }, { "x": 926, "z": 535 }]);
	}
}

initialRallyPointTest((cmpRallyPoint) => {});

initialRallyPointTest((cmpRallyPoint) =>
{
	cmpRallyPoint.Unset();
	return true;
});

// Construction
initialRallyPointTest((cmpRallyPoint) =>
{
	cmpRallyPoint.OnOwnershipChanged({ "from": INVALID_PLAYER, "to": 1 });
	return false;
});

// Capturing
initialRallyPointTest((cmpRallyPoint) =>
{
	cmpRallyPoint.OnOwnershipChanged({ "from": 1, "to": 2 });
	return true;
});

// Destruction
initialRallyPointTest((cmpRallyPoint) =>
{
	cmpRallyPoint.OnOwnershipChanged({ "from": 2, "to": INVALID_PLAYER });
	return false;
});

// Gaia
initialRallyPointTest((cmpRallyPoint) =>
{
	cmpRallyPoint.OnOwnershipChanged({ "from": 2, "to": 0 });
	return true;
});

// Per-player rally point tests
{
	ResetState();
	const entityID = 123;
	let ownerPlayer = 1;
	AddMock(entityID, IID_Ownership, { "GetOwner": () => ownerPlayer });
	const cmpRallyPoint = ConstructComponent(entityID, "RallyPoint", {});
	const player2 = 2;
	const player3 = 3;

	// Initially no per-player positions
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player2), []);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(player2), []);
	TS_ASSERT(!cmpRallyPoint.HasPositions(player2));

	// Add per-player rally point for player 2
	cmpRallyPoint.AddPosition(10, 20, player2);
	cmpRallyPoint.AddData({ "command": "walk" }, player2);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player2), [{ "x": 10, "z": 20 }]);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(player2), [{ "command": "walk" }]);
	TS_ASSERT(cmpRallyPoint.HasPositions(player2));

	// Add a second waypoint for player 2
	cmpRallyPoint.AddPosition(30, 40, player2);
	cmpRallyPoint.AddData({ "command": "garrison" }, player2);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player2),
		[{ "x": 10, "z": 20 }, { "x": 30, "z": 40 }]);

	// Player 3 is unaffected
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player3), []);
	TS_ASSERT(!cmpRallyPoint.HasPositions(player3));

	// Add per-player rally point for player 3
	cmpRallyPoint.AddPosition(50, 60, player3);
	cmpRallyPoint.AddData({ "command": "walk" }, player3);
	TS_ASSERT(cmpRallyPoint.HasPositions(player3));

	// Unset clears player 2 positions and data
	cmpRallyPoint.Unset(player2);
	TS_ASSERT(!cmpRallyPoint.HasPositions(player2));
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player2), []);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(player2), []);
	// Player 3 is unaffected
	TS_ASSERT(cmpRallyPoint.HasPositions(player3));

	// Unset removes player 3 entry
	cmpRallyPoint.Unset(player3);
	TS_ASSERT(!cmpRallyPoint.HasPositions(player3));

	// Per-player data is cleared on ownership change
	cmpRallyPoint.AddPosition(10, 20, player2);
	cmpRallyPoint.AddData({ "command": "walk" }, player2);
	TS_ASSERT(cmpRallyPoint.HasPositions(player2));
	cmpRallyPoint.OnOwnershipChanged({ "from": 1, "to": 2 });
	ownerPlayer = 2;
	TS_ASSERT(!cmpRallyPoint.HasPositions(player2));

	// The owner's rally point entry does not affect allied players' entries
	cmpRallyPoint.AddPosition(100, 200);
	cmpRallyPoint.AddData({ "command": "walk" });
	cmpRallyPoint.AddPosition(300, 400, player3);
	cmpRallyPoint.AddData({ "command": "walk" }, player3);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), [{ "x": 100, "z": 200 }]);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player3), [{ "x": 300, "z": 400 }]);
	// Unset does not affect per-player data
	cmpRallyPoint.Unset();
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(), []);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player3), [{ "x": 300, "z": 400 }]);
}

// Ownership change construction/destruction preserves per-player data
{
	ResetState();
	const entityID = 123;
	const cmpRallyPoint = ConstructComponent(entityID, "RallyPoint", {});
	const player2 = 2;

	cmpRallyPoint.AddPosition(10, 20, player2);
	cmpRallyPoint.AddData({ "command": "walk" }, player2);

	// Construction: from INVALID_PLAYER should not clear per-player data
	cmpRallyPoint.OnOwnershipChanged({ "from": INVALID_PLAYER, "to": 1 });
	TS_ASSERT(cmpRallyPoint.HasPositions(player2));
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player2), [{ "x": 10, "z": 20 }]);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetData(player2), [{ "command": "walk" }]);

	// Destruction: to INVALID_PLAYER should not clear per-player data
	cmpRallyPoint.OnOwnershipChanged({ "from": 1, "to": INVALID_PLAYER });
	TS_ASSERT(cmpRallyPoint.HasPositions(player2));
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPoint.GetPositions(player2), [{ "x": 10, "z": 20 }]);
}

// OnGlobalEntityRenamed migrates per-player rally point data to the new entity
{
	ResetState();
	const oldEntityID = 123;
	const newEntityID = 456;
	const player2 = 2;
	const player3 = 3;

	AddMock(oldEntityID, IID_Ownership, { "GetOwner": () => 1 });
	AddMock(newEntityID, IID_Ownership, { "GetOwner": () => 1 });
	const cmpRallyPointOld = ConstructComponent(oldEntityID, "RallyPoint", {});
	const cmpRallyPointNew = ConstructComponent(newEntityID, "RallyPoint", {});

	cmpRallyPointOld.AddPosition(100, 200);
	cmpRallyPointOld.AddData({ "command": "walk" });
	cmpRallyPointOld.AddPosition(10, 20, player2);
	cmpRallyPointOld.AddData({ "command": "walk" }, player2);
	cmpRallyPointOld.AddPosition(30, 40, player3);
	cmpRallyPointOld.AddData({ "command": "garrison" }, player3);

	cmpRallyPointOld.OnGlobalEntityRenamed({ "entity": oldEntityID, "newentity": newEntityID });

	// New entity receives owner and per-player rally point data
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPointNew.GetPositions(), [{ "x": 100, "z": 200 }]);
	TS_ASSERT(cmpRallyPointNew.HasPositions(player2));
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPointNew.GetPositions(player2), [{ "x": 10, "z": 20 }]);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPointNew.GetData(player2), [{ "command": "walk" }]);
	TS_ASSERT(cmpRallyPointNew.HasPositions(player3));
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPointNew.GetPositions(player3), [{ "x": 30, "z": 40 }]);
	TS_ASSERT_UNEVAL_EQUALS(cmpRallyPointNew.GetData(player3), [{ "command": "garrison" }]);

	// Rename for an unrelated entity does not migrate to new entity
	ResetState();
	const cmpRP1 = ConstructComponent(oldEntityID, "RallyPoint", {});
	const cmpRP2 = ConstructComponent(newEntityID, "RallyPoint", {});
	cmpRP1.AddPosition(10, 20, player2);
	cmpRP1.OnGlobalEntityRenamed({ "entity": 999, "newentity": newEntityID });
	TS_ASSERT(!cmpRP2.HasPositions(player2));
}
