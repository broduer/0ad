Engine.LoadComponentScript("interfaces/Timer.js");
Engine.LoadComponentScript("interfaces/Formation.js");
Engine.LoadComponentScript("Formation.js");

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