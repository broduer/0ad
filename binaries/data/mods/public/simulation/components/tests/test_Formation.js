Engine.LoadHelperScript("Player.js");
Engine.LoadHelperScript("Position.js");
Engine.LoadHelperScript("Sound.js");
Engine.LoadComponentScript("interfaces/Auras.js");
Engine.LoadComponentScript("interfaces/Builder.js");
Engine.LoadComponentScript("interfaces/BuildingAI.js");
Engine.LoadComponentScript("interfaces/Capturable.js");
Engine.LoadComponentScript("interfaces/Diplomacy.js");
Engine.LoadComponentScript("interfaces/Garrisonable.js");
Engine.LoadComponentScript("interfaces/Resistance.js");
Engine.LoadComponentScript("interfaces/Formation.js");
Engine.LoadComponentScript("interfaces/Heal.js");
Engine.LoadComponentScript("interfaces/Health.js");
Engine.LoadComponentScript("interfaces/Pack.js");
Engine.LoadComponentScript("interfaces/ResourceSupply.js");
Engine.LoadComponentScript("interfaces/ResourceGatherer.js");
Engine.LoadComponentScript("interfaces/Timer.js");
Engine.LoadComponentScript("interfaces/Turretable.js");
Engine.LoadComponentScript("interfaces/UnitAI.js");
Engine.LoadComponentScript("Formation.js");
Engine.LoadComponentScript("UnitAI.js");

const entity_id = 5;

AddMock(SYSTEM_ENTITY, IID_Timer, {
	"SetInterval": () => {},
	"SetTimeout": () => {},
});

const formationTemplate = {
	"RequiredMemberCount": 2,
	"DisabledTooltip": "",
	"SpeedMultiplier": 1,
	"FormationShape": "square",
	"MaxTurningAngle": 0,
	"SortingClasses": "Hero Champion Cavalry Melee Ranged",
	"SortingOrder": "fillToTheCenter",
	"ShiftRows": false,
	"UnitSeparationWidthMultiplier": 1,
	"UnitSeparationDepthMultiplier": 1,
	"WidthDepthRatio": 1,
	"Sloppiness": 0
};

const cmpFormation = ConstructComponent(entity_id, "Formation", formationTemplate);

const testingAngles = [];

for (let i = 0; i < 179; i++)
	testingAngles.push(i * Math.PI / 180);

TS_ASSERT(testingAngles.every(x => !cmpFormation.DoesAngleDifferenceAllowTurning(0, x)));
TS_ASSERT(testingAngles.every(x => !cmpFormation.DoesAngleDifferenceAllowTurning(0, -x)));

cmpFormation.maxTurningAngle = Math.PI;

TS_ASSERT(testingAngles.every(x => cmpFormation.DoesAngleDifferenceAllowTurning(0, x)));
TS_ASSERT(testingAngles.every(x => cmpFormation.DoesAngleDifferenceAllowTurning(0, -x)));

// Test GetClosestMemberToPosition and GetClosestMemberToEntity functions
function TestGetClosestMemberFunctions()
{
	ResetState();

	AddMock(SYSTEM_ENTITY, IID_Timer, {
		"SetInterval": () => {},
		"SetTimeout": () => {},
	});

	// Create test members with different positions
	const members = [20, 21, 22, 23];

	AddMock(20, IID_Position, {
		"IsInWorld": () => true,
		"GetPosition2D": () => new Vector2D(0, 0)
	});

	AddMock(21, IID_Position, {
		"IsInWorld": () => true,
		"GetPosition2D": () => new Vector2D(10, 0)
	});

	AddMock(22, IID_Position, {
		"IsInWorld": () => true,
		"GetPosition2D": () => new Vector2D(0, 10)
	});

	AddMock(23, IID_Position, {
		"IsInWorld": () => false, // This member is not in world
		"GetPosition2D": () => new Vector2D(100, 100)
	});

	cmpFormation.members = members;

	// GetClosestMemberToPosition - basic functionality
	const testPosition1 = new Vector2D(1, 1);
	const closest1 = cmpFormation.GetClosestMemberToPosition(testPosition1);
	TS_ASSERT_EQUALS(closest1, 20); // Member 20 should be closest to (1,1)

	// GetClosestMemberToPosition - different position
	const testPosition2 = new Vector2D(9, 1);
	const closest2 = cmpFormation.GetClosestMemberToPosition(testPosition2);
	TS_ASSERT_EQUALS(closest2, 21); // Member 21 should be closest to (9,1)

	// GetClosestMemberToPosition - with filter
	const filterMeleeOnly = (member) => member !== 22; // Exclude member 22
	const closest3 = cmpFormation.GetClosestMemberToPosition(testPosition1, filterMeleeOnly);
	TS_ASSERT_EQUALS(closest3, 20); // Should still be member 20, but filtered from available options

	const filterExclude20 = (member) => member !== 20; // Exclude member 20
	const closest4 = cmpFormation.GetClosestMemberToPosition(testPosition1, filterExclude20);
	TS_ASSERT_EQUALS(closest4, 21); // Should be member 21 since 20 is excluded

	// GetClosestMemberToPosition - member not in world should be ignored
	const testPosition3 = new Vector2D(99, 99);
	const closest5 = cmpFormation.GetClosestMemberToPosition(testPosition3);
	TS_ASSERT(closest5 !== 23); // Member 23 is not in world, so shouldn't be returned
	TS_ASSERT(closest5 === 20 || closest5 === 21 || closest5 === 22); // Should return one of the valid members

	// GetClosestMemberToEntity - basic functionality
	const referenceEntity = 30;
	AddMock(30, IID_Position, {
		"IsInWorld": () => true,
		"GetPosition2D": () => new Vector2D(1, 1)
	});

	const closest6 = cmpFormation.GetClosestMemberToEntity(referenceEntity);
	TS_ASSERT_EQUALS(closest6, 20); // Member 20 should be closest to reference entity at (1,1)

	// GetClosestMemberToEntity - with filter
	const closest7 = cmpFormation.GetClosestMemberToEntity(referenceEntity, filterExclude20);
	TS_ASSERT_EQUALS(closest7, 21); // Should be member 21 since 20 is excluded

	// GetClosestMemberToEntity - invalid reference entity
	const invalidEntity = 31;
	AddMock(31, IID_Position, {
		"IsInWorld": () => false, // Not in world
		"GetPosition2D": () => new Vector2D(1, 1)
	});

	const closest8 = cmpFormation.GetClosestMemberToEntity(invalidEntity);
	TS_ASSERT_EQUALS(closest8, INVALID_ENTITY); // Should return INVALID_ENTITY for invalid reference

	// GetClosestMemberToEntity - no position component on reference
	const noPositionEntity = 32;
	const closest9 = cmpFormation.GetClosestMemberToEntity(noPositionEntity);
	TS_ASSERT_EQUALS(closest9, INVALID_ENTITY); // Should return INVALID_ENTITY

	// Empty formation
	const emptyFormationID = 40;
	const cmpEmptyFormation = ConstructComponent(emptyFormationID, "Formation", formationTemplate);
	cmpEmptyFormation.members = [];

	const closest10 = cmpEmptyFormation.GetClosestMemberToPosition(testPosition1);
	TS_ASSERT_EQUALS(closest10, INVALID_ENTITY); // Should return INVALID_ENTITY for empty formation

	const closest11 = cmpEmptyFormation.GetClosestMemberToEntity(referenceEntity);
	TS_ASSERT_EQUALS(closest11, INVALID_ENTITY); // Should return INVALID_ENTITY for empty formation

	// Test 10: All members filtered out
	const filterNone = (member) => false; // Filter out all members
	const closest12 = cmpFormation.GetClosestMemberToPosition(testPosition1, filterNone);
	TS_ASSERT_EQUALS(closest12, INVALID_ENTITY); // Should return INVALID_ENTITY when all members are filtered

	const closest13 = cmpFormation.GetClosestMemberToEntity(referenceEntity, filterNone);
	TS_ASSERT_EQUALS(closest13, INVALID_ENTITY); // Should return INVALID_ENTITY when all members are filtered
}

TestGetClosestMemberFunctions();


function TestIsRearrangementAllowed()
{
	ResetState();

	const playerID = 1;
	const playerEntity = 5;

	AddMock(SYSTEM_ENTITY, IID_PlayerManager, {
		"GetPlayerByID": id => playerEntity,
		"GetNumPlayers": () => 2
	});

	AddMock(playerEntity, IID_Player, {
		"GetPlayerID": () => playerID
	});

	AddMock(SYSTEM_ENTITY, IID_Timer, {
		"SetInterval": () => {},
		"SetTimeout": () => {}
	});

	// Helper to mock a unit's state
	function mockUnitState(entityID, state)
	{
		AddMock(entityID, IID_UnitAI, {
			"GetCurrentState": () => state
		});
	}

	// Controller in COMBAT.ATTACKING should block rearrangement
	(function() {
		const controllerID = cmpFormation.entity;

		cmpFormation.members = [201, 202];

		// Mock controller in combat state
		mockUnitState(controllerID, "COMBAT.ATTACKING");
		// Mock members (state doesn't matter since controller blocks)
		mockUnitState(201, "IDLE");
		mockUnitState(202, "IDLE");

		// Controller in combat should block
		TS_ASSERT(!cmpFormation.IsRearrangementAllowed());
	})();

	// Different critical states sum should count toward threshold
	(function() {
		const controllerID = cmpFormation.entity;

		// Mock controller in walking state
		mockUnitState(controllerID, "WALKING");

		// Create exactly 100 members, all starting as idle
		const members = [];
		for (let i = 0; i < 100; i++)
		{
			const memberID = 5000 + i;
			// All start idle
			mockUnitState(memberID, "IDLE");
			members.push(memberID);
		}

		// Critical states to test
		const criticalStates = [
			"COMBAT.ATTACKING",
			"COMBAT.CHASING",
			"COMBAT.APPROACHING",
			"HEAL.HEALING",
			"GATHER.RETURNING",
			"REPAIR.REPAIRING"
		];

		// Helper to set first N members as critical (using different states)
		function setCriticalMembers(count)
		{
			for (let i = 0; i < members.length; i++)
			{
				const memberID = members[i];
				if (i < count)
				{
					// Use different critical states cyclically
					const state = criticalStates[i % criticalStates.length];
					mockUnitState(memberID, state);
				}
				else
				{
					mockUnitState(memberID, "IDLE");
				}
			}
		}

		cmpFormation.members = members;

		// 0 critical members = 0% → should allow
		setCriticalMembers(0);
		TS_ASSERT(cmpFormation.IsRearrangementAllowed());

		// 1 critical member = 1% → should allow
		setCriticalMembers(1);
		TS_ASSERT(cmpFormation.IsRearrangementAllowed());

		// 4 critical members = 4% → should allow (< 5%)
		setCriticalMembers(4);
		TS_ASSERT(cmpFormation.IsRearrangementAllowed());

		// 5 critical members = 5% → should allow (= 5%, not > 5%)
		setCriticalMembers(5);
		TS_ASSERT(cmpFormation.IsRearrangementAllowed());

		// 6 critical members = 6% → should block (> 5%)
		setCriticalMembers(6);
		TS_ASSERT(!cmpFormation.IsRearrangementAllowed());

		// 50 critical members = 50% → should block
		setCriticalMembers(50);
		TS_ASSERT(!cmpFormation.IsRearrangementAllowed());

		// All 100 critical members = 100% → should block
		setCriticalMembers(100);
		TS_ASSERT(!cmpFormation.IsRearrangementAllowed());
	})();
}

TestIsRearrangementAllowed();