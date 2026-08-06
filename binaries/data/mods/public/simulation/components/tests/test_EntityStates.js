Engine.LoadHelperScript("EntityStateRetriever.js");
Engine.LoadHelperScript("ObstructionSnap.js");
Engine.LoadHelperScript("Player.js");
Engine.LoadHelperScript("ValueModification.js");
Engine.LoadComponentScript("interfaces/BuildingAI.js");
Engine.LoadComponentScript("interfaces/Capturable.js");
Engine.LoadComponentScript("interfaces/Formation.js");
Engine.LoadComponentScript("interfaces/Foundation.js");
Engine.LoadComponentScript("interfaces/Garrisonable.js");
Engine.LoadComponentScript("interfaces/GarrisonHolder.js");
Engine.LoadComponentScript("interfaces/Gate.js");
Engine.LoadComponentScript("interfaces/Guard.js");
Engine.LoadComponentScript("interfaces/Heal.js");
Engine.LoadComponentScript("interfaces/Health.js");
Engine.LoadComponentScript("interfaces/Loot.js");
Engine.LoadComponentScript("interfaces/Market.js");
Engine.LoadComponentScript("interfaces/ModifiersManager.js");
Engine.LoadComponentScript("interfaces/Pack.js");
Engine.LoadComponentScript("interfaces/Population.js");
Engine.LoadComponentScript("interfaces/ProductionQueue.js");
Engine.LoadComponentScript("interfaces/Promotion.js");
Engine.LoadComponentScript("interfaces/Repairable.js");
Engine.LoadComponentScript("interfaces/Researcher.js");
Engine.LoadComponentScript("interfaces/Resistance.js");
Engine.LoadComponentScript("interfaces/ResourceDropsite.js");
Engine.LoadComponentScript("interfaces/ResourceGatherer.js");
Engine.LoadComponentScript("interfaces/ResourceTrickle.js");
Engine.LoadComponentScript("interfaces/ResourceSupply.js");
Engine.LoadComponentScript("interfaces/Trader.js");
Engine.LoadComponentScript("interfaces/Trainer.js");
Engine.LoadComponentScript("interfaces/TurretHolder.js");
Engine.LoadComponentScript("interfaces/Turretable.js");
Engine.LoadComponentScript("interfaces/StatusEffectsReceiver.js");
Engine.LoadComponentScript("interfaces/UnitAI.js");
Engine.LoadComponentScript("interfaces/Upgrade.js");
Engine.LoadComponentScript("interfaces/Upkeep.js");
Engine.LoadComponentScript("GuiInterface.js");
Engine.LoadComponentScript("ModifiersManager.js");
Engine.LoadComponentScript("RallyPoint.js");

// This script tests the GetEntityState and GetTemplateData methods of the GuiInterface component and the underlying
// logic of the EntityStateRetriever, ModifiersManager, and globalscripts/Templates.js.
// This data is not actually serialised, but passed to the GUI. However, it is essential that it is complete and
// crucial for performance that each call returns as little data as necessary, that nothing is computed twice.
// See helpers/EntityStateRetriever.js for details.

const cmpGuiInterface = ConstructComponent(SYSTEM_ENTITY, "GuiInterface");
const cmpModifiersManager = ConstructComponent(SYSTEM_ENTITY, "ModifiersManager");

const templateNames = {};
const templates = {};
AddMock(SYSTEM_ENTITY, IID_TemplateManager, {
	"GetCurrentTemplateName": function(ent) { return templateNames[ent]; },
	"GetTemplate": function(name) { return templates[name]; }
});

templates.example_structure_template = {
	"@parent": "parent_structure_emplate",
	"AlertRaiser": {
		"List": { "@datatype": "tokens", "_string": "UnitClass1 UnitClass2" },
		"RaiseAlertRange": 120, "EndOfAlertRange": 180, "SearchRange": 100
	},
	"Attack": {
		"Ranged": {
			"AttackName": "Bow", "Damage": { "Pierce": "8" },
			"MaxRange": "60", "PrepareTime": "400",
			"PreferredClasses": { "@datatype": "tokens", "_string": "Human" },
			"Projectile": {
				"FriendlyFire": "false", "Gravity": "50",
				"LaunchPoint": { "@y": "0" },
				"Speed": "100", "Spread": "2"
			},
			"RepeatTime": "4000"
		}
	},
	"Auras": { "@datatype": "tokens", "_string": "aura1" },
	"BuildingAI": {
		"DefaultArrowCount": "6", "GarrisonArrowClasses": "UnitClass1",
		"GarrisonArrowMultiplier": "1"
	},
	"BuildRestrictions": {
		"Territory": "own ally", "PlacementType": "shore",
		"Category": "Structure1",
		"Distance": { "FromClass": "StructureClass0", "MinDistance": 100 }
	},
	"Capturable": {
		"CapturePoints": "2500", "GarrisonRegenRate": "1.0", "RegenRate": "30"
	},
	"Cost": {
		"BuildTime": "500", "Population": "0",
		"Resources": { "food": "0", "metal": "250", "stone": "300", "wood": "300" }
	},
	"DeathDamage": {
		"Shape": "Circular", "Range": 20, "FriendlyFire": "true", "Damage": { "Fire": 500 }
	},
	"Decay": {
		"Active": "false", "DelayTime": "0.0", "SinkAccel": "9.8",
		"SinkProb": "1.0", "SinkRate": "3.0", "SinkingAnim": "false"
	},
	"GarrisonHolder": {
		"BuffHeal": "1", "EjectHealth": "0.1",
		"EjectClassesOnDestroy": { "@datatype": "tokens", "_string": "Unit" },
		"List": { "@datatype": "tokens", "_string": "Support Infantry Cavalry" },
		"LoadingRange": "1", "Max": "20"
	},
	"Health": {
		"DamageVariants": {
			"heavydamage": "0.35", "lightdamage": "0.85", "mediumdamage": "0.65"
		},
		"DeathType": "corpse", "IdleRegenRate": "0",
		"Max": "3000", "RegenRate": "0",
		"SpawnEntityOnDeath": "decay|rubble/rubble_stone_6x6",
		"Unhealable": "true"
	},
	"Identity": {
		"Civ": "civ1",
		"Classes": { "@datatype": "tokens", "_string": "StructureClass1 StructureClass2 StructureClass3" },
		"GenericName": "Example Structure",
		"Icon": "example_structure.png",
		"SelectionGroupName": "example_structure",
		"Tooltip": "This is an example structure.",
		"Undeletable": "false",
		"VisibleClasses": { "@datatype": "tokens", "_string": "StructureVisibleClass1 StructureVisibleClass2" }
	},
	"Loot": { "food": "10", "metal": "50", "stone": "60", "wood": "60", "xp": "30" },
	"Population": { "Bonus": 5 },
	"Position": {
		"Altitude": "0", "Anchor": "upright", "FloatDepth": "0",
		"Floating": "false", "TurnRate": "6"
	},
	"ProductionQueue": {},
	"Repairable": { "RepairTimeRatio": "2.0" },
	"Researcher": {
		"Technologies": { "@datatype": "tokens", "_string": "tech1 tech2 tech3" }
	},
	"Resistance": {
		"Entity": {
			"ApplyStatus": { "Poisoned": { "BlockChance": "1", "Duration": "0.0" } },
			"Damage": { "Crush": "3", "Hack": "29", "Pierce": "35" }
		},
		"Foundation": {
			"ApplyStatus": { "Poisoned": { "BlockChance": "1", "Duration": "0.0" } },
			"Damage": { "Crush": "1", "Hack": "1", "Pierce": "10" }
		}
	},
	"ResourceDropsite": { "Sharable": "true", "Types": "food stone metal" },
	"ResourceTrickle": { "Rates": { "food": 1.3 }, "Interval": 1500 },
	"TerritoryDecay": { "DecayRate": "20", "Territory": "neutral enemy" },
	"TerritoryInfluence": { "Radius": "140", "Root": "true", "Weight": "10000" },
	"Trainer": {
		"BatchTimeModifier": "0.8",
		"Entities": { "@datatype": "tokens", "_string": "template1 template2 template3" }
	},
	"Visibility": {
		"AlwaysVisible": "false", "Corpse": "false",
		"Preview": "false", "RetainInFog": "true"
	},
	"Vision": { "Range": "90" },
	"VisionSharing": { "Bribable": "false" }
};
templates.example_unit_template = {
	"@parent": "parent_unit_template",
	"Attack": {
		"Capture": {
			"AttackName": "Capture", "Capture": "2.5",
			"MaxRange": "4", "RepeatTime": "1000",
			"RestrictedClasses": { "@datatype": "tokens", "_string": "RestrictedClass1" }
		},
		"Melee": {
			"AttackName": "Spear", "MaxRange": "4",
			"Bonuses": { "BonusCavMelee": { "Classes": "Cavalry", "Multiplier": "2.5" } },
			"Damage": { "Hack": "4.5", "Pierce": "4" },
			"PreferredClasses": { "@datatype": "tokens", "_string": "PreferredClass1" },
			"PrepareTime": "500", "RepeatTime": "1000"
		},
		"Slaughter": {
			"AttackName": "Slaughter", "Damage": { "Hack": "1000" },
			"MaxRange": "2", "PrepareTime": "900", "RepeatTime": "1000",
		}
	},
	"Builder": {
		"Entities": {
			"@datatype": "tokens",
			"_string": "buildable1 buildable2 buildable3 buildable4"
		},
		"Rate": "1.0"
	},
	"Cost": {
		"BuildTime": "10", "Population": "1",
		"Resources": { "food": "50", "metal": "0", "stone": "0", "wood": "50" }
	},
	"Garrisonable": { "Size": "1" },
	"Heal": {
		"Range": 20, "Health": 10, "Interval": 2500,
		"UnhealableClasses": { "@datatype": "tokens", "_string": "" },
		"HealableClasses": { "@datatype": "tokens", "_string": "HealableClass1" },
		"LineTexture": "heal_line_texture.png",
		"LineTextureMask": "heal_line_mask.png",
		"LineThickness": 0.36
	},
	"Health": {
		"DeathType": "corpse", "IdleRegenRate": "0",
		"Max": "100", "RegenRate": "0", "Unhealable": "false"
	},
	"Identity": {
		"Civ": "civ2", "Lang": "greek", "Rank": "Basic",
		"Classes": { "@datatype": "tokens", "_string": "UnitClass1 UnitClass2 UnitClass3" },
		"GenericName": "Example Unit",
		"Icon": "example_unit.png",
		"Phenotype": { "@datatype": "tokens", "_string": "male" },
		"SelectionGroupName": "example_unit",
		"Tooltip": "This is an example unit.",
		"Undeletable": "false",
		"VisibleClasses": {
			"@datatype": "tokens",
			"_string": "VisibleUnitClass1 VisibleUnitClass2"
		}
	},
	"Loot": { "food": "5", "metal": "4", "stone": "3", "wood": "5", "xp": "100" },
	"Position": {
		"Altitude": "0", "Anchor": "upright",
		"FloatDepth": "0.0", "Floating": "false",
		"TurnRate": "14"
	},
	"Promotion": { "Entity": "promoted_unit_template", "RequiredXp": "100" },
	"Resistance": {
		"Entity": { "Damage": { "Crush": "15", "Hack": "3", "Pierce": "3" } }
	},
	"ResourceGatherer": {
		"BaseSpeed": "1.0",
		"Capacities": { "food": "10", "metal": "11", "stone": "12", "wood": "13" },
		"MaxDistance": "2.0",
		"Rates": {
			"food.fruit": "0.5", "food.grain": "0.6", "food.meat": "0.7",
			"metal.ore": "0.8", "metal.ruins": "0.9", "stone.rock": "1.0",
			"stone.ruins": "1.1", "wood.ruins": "1.2", "wood.tree": "1.3"
		}
	},
	"TreasureCollector": { "MaxDistance": "2" },
	"UnitAI": {
		"CanGuard": "true", "CanPatrol": "true",
		"CheeringTime": "2800",
		"DefaultStance": "aggressive",
		"FleeDistance": "12.0",
		"FormationController": "false",
		"Formations": { "@datatype": "tokens", "_string": "formation1 formation2 formation3" },
		"PatrolWaitTime": "1"
	},
	"UnitMotion": {
		"Acceleration": "35", "FormationController": "false",
		"InstantTurnAngle": "1.5",
		"PassabilityClass": "default",
		"RunMultiplier": "2",
		"WalkSpeed": "9.5",
		"Weight": "10"
	},
	"Visibility": {
		"AlwaysVisible": "false", "Corpse": "false",
		"Preview": "false", "RetainInFog": "false"
	},
	"Vision": { "Range": "80" },
	"VisionSharing": { "Bribable": "false" }
};

const expectedTemplateData = {
	"example_structure_template": {
		"alertRaiser": { "classes": "UnitClass1 UnitClass2" },
		"attack": {
			"Ranged": {
				"Damage": { "Pierce": 8 }, "attackName": { "name": "Bow" },
				"minRange": 0, "maxRange": 60, "yOrigin": 0, "elevationAdaptedRange": 60,
				"repeatTime": 4000, "projectileCount": 1, "friendlyFire": false
			}
		},
		"auras": {
			"aura1": {
				"name": { "generic": "Aura 1" }, "description": "Aura Description",
				"modifications": { "value": "Resistance/Damage/Crush", "add": 1 }, "radius": 30 }
		},
		"buildingAI": {
			"defaultArrowCount": 6, "maxArrowCount": 0,
			"garrisonArrowMultiplier": 1, "garrisonArrowClasses": ["UnitClass1"]
		},
		"buildRestrictions": {
			"placementType": "shore", "territory": "own ally", "category": "Structure1",
			"distance": { "fromClass": "StructureClass0", "min": 100 }
		},
		"maxCapturePoints": 2500,
		"cost": { "food": 0, "metal": 250, "stone": 300, "wood": 300, "population": 0, "time": 500 },
		"deathDamage": { "Damage": { "Fire": 500 }, "friendlyFire": true },
		"garrisonHolder": { "allowedClasses": "Support Infantry Cavalry", "buffHeal": 1, "capacity": 20 },
		"maxHitpoints": 3000,
		"selectionGroupName": "example_structure",
		"name": { "specific": "Example Structure", "generic": "Example Structure" },
		"icon": "example_structure.png",
		"tooltip": "This is an example structure.",
		"identityClasses": ["StructureClass1", "StructureClass2", "StructureClass3",
			"StructureVisibleClass1", "StructureVisibleClass2"],
		"visibleIdentityClasses": ["StructureVisibleClass1", "StructureVisibleClass2"],
		"nativeCiv": "civ1",
		"requirements": undefined,
		"rank": undefined,
		"undeletable": "false",
		"enablesBartering": false,
		"loot": { "food": 10, "metal": 50, "stone": 60, "wood": 60, "xp": 30 },
		"population": { "bonus": 5 },
		"researcher": {
			"techCostMultiplier": { "food": 1, "metal": 1, "stone": 1, "wood": 1, "time": 1 }
		},
		"resistance": {
			"Damage": { "Crush": 3, "Hack": 29, "Pierce": 35 },
			"ApplyStatus": { "Poisoned": { "blockChance": 1, "duration": 0 } }
		},
		"resourceDropsite": { "types": ["food", "stone", "metal"] },
		"resourceTrickle": { "interval": 1500, "rates": { "food": 1.3 } }
	},

	"example_unit_template": {
		"attack": {
			"Capture": {
				"Capture": 2.5, "attackName": { "name": "Capture" },
				"minRange": 0, "maxRange": 4, "yOrigin": 0,
				"elevationAdaptedRange": 4, "repeatTime": 1000
			},
			"Melee": {
				"Damage": { "Hack": 4.5, "Pierce": 4 }, "attackName": { "name": "Spear" },
				"minRange": 0, "maxRange": 4, "yOrigin": 0,
				"elevationAdaptedRange": 4, "repeatTime": 1000
			},
			"Slaughter": {
				"Damage": { "Hack": 1000 }, "attackName": { "name": "Slaughter" },
				"minRange": 0, "maxRange": 2, "yOrigin": 0,
				"elevationAdaptedRange": 2, "repeatTime": 1000
			}
		},
		"builder": true,
		"cost": { "food": 50, "metal": 0, "stone": 0, "wood": 50, "population": 1, "time": 10 },
		"garrisonable": { "size": 1 },
		"heal": {
			"health": 10, "range": 20, "interval": 2500, "unhealableClasses": "", "healableClasses": "HealableClass1"
		},
		"maxHitpoints": 100,
		"selectionGroupName": "example_unit",
		"name": { "specific": "Example Unit", "generic": "Example Unit" },
		"icon": "example_unit.png",
		"tooltip": "This is an example unit.",
		"identityClasses": [
			"UnitClass1", "UnitClass2", "UnitClass3",
			"VisibleUnitClass1", "VisibleUnitClass2", "Basic"
		],
		"visibleIdentityClasses": ["VisibleUnitClass1", "VisibleUnitClass2"],
		"nativeCiv": "civ2",
		"requirements": undefined,
		"rank": "Basic",
		"undeletable": "false",
		"enablesBartering": false,
		"loot": { "food": 5, "metal": 4, "stone": 3, "wood": 5, "xp": 100 },
		"promotion": { "req": 100 },
		"resistance": { "Damage": { "Crush": 15, "Hack": 3, "Pierce": 3 } },
		"resourceGatherRates": {
			"food.fruit": 0.5, "food.grain": 0.6, "food.meat": 0.7,
			"metal.ore": 0.8, "metal.ruins": 0.9,
			"stone.rock": 1, "stone.ruins": 1.1,
			"wood.ruins": 1.2, "wood.tree": 1.3
		},
		"treasureCollector": true,
		"unitAI": { "formations": ["formation1", "formation2", "formation3"] },
		"unitMotion": { "walk": 9.5, "run": 19, "acceleration": 35 }
	}
};

global.AuraTemplates = { "GetAll": () => ({
	"aura1": {
		"auraName": "Aura 1",
		"auraDescription": "Aura Description",
		"modifications": { "value": "Resistance/Damage/Crush", "add": 1 },
		"radius": 30
	}
}) };

Resources = {
	"GetCodes": () => ["food", "metal", "stone", "wood"],
	"GetNames": () => ({
		"food": "Food",
		"metal": "Metal",
		"stone": "Stone",
		"wood": "Wood"
	}),
	"GetResource": resource => ({
		"aiAnalysisInfluenceGroup":
			resource == "food" ? "ignore" :
				resource == "wood" ? "abundant" : "sparse"
	})
};

const PLAYER_1_ENT_ID = 101;
const PLAYER_2_ENT_ID = 102;
cmpModifiersManager.OnGlobalPlayerEntityChanged({ "player": 1, "from": INVALID_PLAYER, "to": PLAYER_1_ENT_ID });
cmpModifiersManager.OnGlobalPlayerEntityChanged({ "player": 2, "from": INVALID_PLAYER, "to": PLAYER_2_ENT_ID });

AddMock(SYSTEM_ENTITY, IID_PlayerManager, {
	"GetPlayerByID": (id) => 100 + id
});

AddMock(PLAYER_1_ENT_ID, IID_Identity, {
	"GetCiv": () => "civ1"
});
AddMock(PLAYER_1_ENT_ID, IID_Player, {
	"GetPlayerID": () => 1
});

AddMock(PLAYER_2_ENT_ID, IID_Identity, {
	"GetCiv": () => "civ1"
});
AddMock(PLAYER_2_ENT_ID, IID_Player, {
	"GetPlayerID": () => 2
});

TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetTemplateData(1, { "templateName": "example_structure_template" }), expectedTemplateData.example_structure_template);
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetTemplateData(1, { "templateName": "example_unit_template" }), expectedTemplateData.example_unit_template);
const TEST_ENTITY_STRUCTURE = 11;
const TEST_ENTITY_UNIT_1 = 21;
const TEST_ENTITY_UNIT_2 = 22;
templateNames[TEST_ENTITY_STRUCTURE] = "example_structure_template";
templateNames[TEST_ENTITY_UNIT_1] = "example_unit_template";
templateNames[TEST_ENTITY_UNIT_2] = "example_unit_template";

const AddMockToEnts = (ents, iid, mock) =>
{
	for (const ent of ents)
		AddMock(ent, iid, mock);
};

AddMock(SYSTEM_ENTITY, IID_RangeManager, {
	"GetLosVisibility": () => "visible",
	"GetElevationAdaptedRange": () => 80,
	"GetEntitiesByPlayer": (player) => player == 1 ? [TEST_ENTITY_STRUCTURE, TEST_ENTITY_UNIT_1, TEST_ENTITY_UNIT_2] : []
});
AddMockToEnts([TEST_ENTITY_STRUCTURE, TEST_ENTITY_UNIT_1, TEST_ENTITY_UNIT_2], IID_Ownership, {
	"GetOwner": () => 1
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Position, {
	"GetTurretParent": () => INVALID_ENTITY,
	"GetPosition": () => new Vector3D(1, 2, 3),
	"GetRotation": () => new Vector3D(0, 1, 0),
	"IsInWorld": () => true
});
AddMock(TEST_ENTITY_UNIT_1, IID_Position, {
	"GetTurretParent": () => INVALID_ENTITY,
	"GetPosition": () => new Vector3D(10, 12, 19),
	"GetRotation": () => new Vector3D(0, 1, 0),
	"IsInWorld": () => true
});
AddMock(TEST_ENTITY_UNIT_2, IID_Position, {
	"GetTurretParent": () => INVALID_ENTITY,
	"GetPosition": () => new Vector3D(20, 22, 24),
	"GetRotation": () => new Vector3D(0, 1, 1),
	"IsInWorld": () => true
});
const structureAttack = expectedTemplateData.example_structure_template.attack;
AddMock(TEST_ENTITY_STRUCTURE, IID_Attack, {
	"GetAttackTypes": () => Object.keys(structureAttack),
	"GetRange": (type) => ({ "min": structureAttack[type].minRange, "max": structureAttack[type].maxRange }),
	"GetAttackYOrigin": (type) => structureAttack[type].yOrigin
});
AddMockToEnts([TEST_ENTITY_UNIT_1, TEST_ENTITY_UNIT_2], IID_Attack, {
	"GetAttackTypes": () => Object.keys(expectedTemplateData.example_unit_template.attack)
});
AddMock(TEST_ENTITY_STRUCTURE, IID_BuildingAI, {
	"GetArrowCount": () => 10
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Capturable, {
	"GetCapturePoints": () => 1567
});
AddMockToEnts([TEST_ENTITY_UNIT_1, TEST_ENTITY_UNIT_2], IID_Garrisonable, {
	"HolderID": () => INVALID_ENTITY
});
AddMock(TEST_ENTITY_STRUCTURE, IID_GarrisonHolder, {
	"GetEntities": () => [51, 52, 53],
	"OccupiedSlots": () => 3
});
AddMock(TEST_ENTITY_UNIT_1, IID_Guard, {
	"GetEntities": () => [TEST_ENTITY_STRUCTURE]
});
AddMock(TEST_ENTITY_UNIT_2, IID_Guard, {
	"GetEntities": () => []
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Health, {
	"GetHitpoints": () => 2139,
	"IsRepairable": () => true,
	"IsInjured": () => true,
	"IsUnhealable": () => true
});
AddMock(TEST_ENTITY_UNIT_1, IID_Health, {
	"GetHitpoints": () => 39,
	"IsRepairable": () => false,
	"IsInjured": () => true,
	"IsUnhealable": () => false
});
AddMock(TEST_ENTITY_UNIT_2, IID_Health, {
	"GetHitpoints": () => 100,
	"IsRepairable": () => false,
	"IsInjured": () => false,
	"IsUnhealable": () => false
});
AddMockToEnts([TEST_ENTITY_UNIT_1, TEST_ENTITY_UNIT_2], IID_Identity, {
	"GetClassesList": () => expectedTemplateData.example_unit_template.identityClasses,
	"IsControllable": () => true
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Identity, {
	"GetClassesList": () => expectedTemplateData.example_structure_template.identityClasses,
	"IsControllable": () => true
});
AddMock(TEST_ENTITY_STRUCTURE, IID_ProductionQueue, {
	"GetQueue": () => [{
		"unitTemplate": "trainable1", "count": 2, "neededSlots": 0, "progress": 0.375,
		"timeRemaining": 5000, "paused": undefined, "metadata": undefined, "id": 12
	}],
	"IsAutoQueueing": () => true
});
AddMock(TEST_ENTITY_UNIT_1, IID_Promotion, {
	"GetCurrentXp": () => 89
});
AddMock(TEST_ENTITY_UNIT_2, IID_Promotion, {
	"GetCurrentXp": () => 56
});
AddMock(TEST_ENTITY_STRUCTURE, IID_RallyPoint, {
	"GetPositions": () => [{ "x": 10, "y": 50 }, { "x": 20, "y": 40 }]
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Repairable, {
	"GetNumBuilders": () => 2,
	"GetBuildTime": () => 234
});
AddMock(TEST_ENTITY_UNIT_1, IID_ResourceGatherer, {
	"GetCarryingStatus": () => [ { "type": "wood", "amount": 7, "max": 13 } ]
});
AddMock(TEST_ENTITY_UNIT_2, IID_ResourceGatherer, {
	"GetCarryingStatus": () => [ { "type": "wood", "amount": 2, "max": 13 } ]
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Researcher, {
	"GetTechnologiesList": () => ["tech1", { "pair": ["tech2_1", "tech2_2"] }]
});
AddMock(TEST_ENTITY_STRUCTURE, IID_Trainer, {
	"GetEntitiesList": () => ["trainable1", "trainable2", "trainable3"]
});
const FORMATION_CONTROLLER_ENT_ID = 43;
AddMockToEnts([TEST_ENTITY_UNIT_1, TEST_ENTITY_UNIT_2], IID_UnitAI, {
	"GetCurrentState": () => "INDIVIDUAL.IDLE",
	"GetOrders": () => [{ "type": "Walk", "data": { "x": 11, "z": 22, "force": true, "relaxed": true } }],
	"HasWorkOrders": () => true,
	"IsGuardOf": () => false,
	"IsIdle": () => true,
	"GetFormationController": () => FORMATION_CONTROLLER_ENT_ID
});

const expectedDynamicStates = {
	[TEST_ENTITY_STRUCTURE]: {
		"id": TEST_ENTITY_STRUCTURE,
		"player": 1,
		"templateName": "example_structure_template",
		"attack": { "Ranged": { "elevationAdaptedRange": 80 } },
		"buildingAI": { "arrowCount": 10 },
		"capturePoints": 1567,
		"controllable": true,
		"garrisonHolder": { "entities": [51, 52, 53], "occupiedSlots": 3 },
		"hitpoints": 2139,
		"needsRepair": true,
		"needsHeal": false,
		"position": { "x": 1, "y": 2, "z": 3 },
		"production": {
			"queue": [{
				"unitTemplate": "trainable1", "count": 2, "neededSlots": 0, "progress": 0.375,
				"timeRemaining": 5000, "paused": undefined, "metadata": undefined, "id": 12
			}],
			"autoqueue": true
		},
		"rallyPoint": { "position": { "x": 10, "y": 50 } },
		"repairable": { "numBuilders": 2, "buildTime": 234 },
		"researcher": { "technologies": ["tech1", {	"pair": ["tech2_1", "tech2_2"] }] },
		"trainer": { "entities": ["trainable1", "trainable2", "trainable3"] },
		"visibility": "visible"
	},
	[TEST_ENTITY_UNIT_1]: {
		"id": TEST_ENTITY_UNIT_1,
		"player": 1,
		"templateName": "example_unit_template",
		"controllable": true,
		"garrisonable": { "holder": INVALID_ENTITY },
		"guard": { "entities": [TEST_ENTITY_STRUCTURE] },
		"hitpoints": 39,
		"needsRepair": false,
		"needsHeal": true,
		"position": { "x": 10, "y": 12, "z": 19 },
		"promotion": { "curr": 89 },
		"resourceCarrying": [{ "type": "wood", "amount": 7, "max": 13 }],
		"unitAI": {
			"state": "INDIVIDUAL.IDLE",
			"orders": [{ "type": "Walk", "data": { "x": 11, "z": 22, "force": true, "relaxed": true } }],
			"hasWorkOrders": true,
			"isGuarding": false,
			"isIdle": true,
			"formationController": FORMATION_CONTROLLER_ENT_ID
		},
		"visibility": "visible"
	},
	[TEST_ENTITY_UNIT_2]: {
		"id": TEST_ENTITY_UNIT_2,
		"player": 1,
		"templateName": "example_unit_template",
		"controllable": true,
		"garrisonable": { "holder": INVALID_ENTITY },
		"guard": { "entities": [] },
		"hitpoints": 100,
		"needsRepair": false,
		"needsHeal": true,
		"position": { "x": 20, "y": 22, "z": 24 },
		"promotion": { "curr": 56 },
		"resourceCarrying": [{ "type": "wood", "amount": 2, "max": 13 }],
		"unitAI": {
			"state": "INDIVIDUAL.IDLE",
			"orders": [{ "type": "Walk", "data": { "x": 11, "z": 22, "force": true, "relaxed": true } }],
			"hasWorkOrders": true,
			"isGuarding": false,
			"isIdle": true,
			"formationController": FORMATION_CONTROLLER_ENT_ID
		},
		"visibility": "visible"
	}
};

// The template data of both templates has been retrieved already, so it shouldn't be computed again now.
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_STRUCTURE), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_STRUCTURE]
});
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_1), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_1]
});

Engine.PostMessage = (ent, mtid, msg) =>
{
	if (mtid == MT_TemplateModification && ent == SYSTEM_ENTITY)
		cmpGuiInterface.OnTemplateModification(msg);
};

// This makes the previously returned templateData out-of-date, so it should now be recomputed (once) the next time
// queried for it.
cmpModifiersManager.AddModifier(
	"Attack/Ranged/Damage/Pierce", "DamageBuff", [{ "affects": ["StructureClass2"], "add": 1 }], PLAYER_1_ENT_ID
);

expectedTemplateData.example_structure_template.attack.Ranged.Damage.Pierce += 1;

TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_STRUCTURE), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_STRUCTURE],
	"templateData": expectedTemplateData.example_structure_template
});
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_1), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_1],
	"templateData": expectedTemplateData.example_unit_template
});
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_2), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_2]
});

// On the other hand, changes of other playes' modifiers should be ignored.
cmpGuiInterface.OnTemplateModification({ "player": 2 });
TS_ASSERT(!cmpGuiInterface.GetEntityState(1, TEST_ENTITY_STRUCTURE).templateData);


const increasedMaxHPModifiers = [{ "affects": ["UnitClass1", "StructureClass1"], "add": 50 }];
cmpModifiersManager.AddModifier("Health/Max", "IncreasedMaxHP", increasedMaxHPModifiers, TEST_ENTITY_UNIT_1);
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_1), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_1],
	"modificationsID": "Health/Max:IncreasedMaxHP(1) ",
	// New modificationsID, so modifiedTemplateData should be computed. Only for the affected components, though.
	"modifiedTemplateData": { "maxHitpoints": 150 }
});

cmpModifiersManager.AddModifier("Health/Max", "IncreasedMaxHP", increasedMaxHPModifiers, TEST_ENTITY_UNIT_2);
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_2), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_2],
	"modificationsID": "Health/Max:IncreasedMaxHP(1) "
	// modifiedTemplateData for owner 1, example_unit_template and the above modificationsID has been returned in the
	// previous call already, so it shouldn't be computed again now.
});

cmpModifiersManager.AddModifier("Promotion/RequiredXp", "FasterPromotion", [{ "affects": ["UnitClass2"], "add": -10 }], TEST_ENTITY_UNIT_2, true);
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_2), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_2],
	"modificationsID": "Health/Max:IncreasedMaxHP(1) Promotion/RequiredXp:FasterPromotion(1) ",
	// New modificationsID, so modifiedTemplateData should be computed again.
	"modifiedTemplateData": { "maxHitpoints": 150, "promotion": { "req": 90 } }
});

cmpModifiersManager.AddModifier("Health/Max", "IncreasedMaxHP", increasedMaxHPModifiers, TEST_ENTITY_STRUCTURE);
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_STRUCTURE), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_STRUCTURE],
	"modificationsID": "Health/Max:IncreasedMaxHP(1) ",
	// modifiedTemplateData for owner 1 and the above modificationsID has been returned in a previous call but only for
	// different template (example_unit_template and not example_structure_template), so it should be computed again now.
	"modifiedTemplateData": { "maxHitpoints": 3050 }
});


// This makes any previously returned templateData and modifiedTemplateData out-of-date, so they should now be recomputed
// (once) the next time queried for them.
cmpModifiersManager.AddModifier("Health/Max", "DecreasedMaxHP", [{ "affects": ["UnitClass1"], "add": -1 }], PLAYER_1_ENT_ID);
expectedTemplateData.example_unit_template.maxHitpoints -= 1;
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_1), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_1],
	"templateData": expectedTemplateData.example_unit_template,
	"modificationsID": "Health/Max:IncreasedMaxHP(1) ",
	"modifiedTemplateData": { "maxHitpoints": 149 }
});
cmpModifiersManager.RemoveModifier("Promotion/RequiredXp", "FasterPromotion", TEST_ENTITY_UNIT_2);
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_2), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_2],
	"modificationsID": "Health/Max:IncreasedMaxHP(1) ",
	// modifiedTemplateData for owner 1, example_unit_template and the above modificationsID has been returned in the
	// previous call already, so it shouldn't be computed again now.
});

AddMock(TEST_ENTITY_UNIT_2, IID_Ownership, {
	"GetOwner": () => 2
});
expectedDynamicStates[TEST_ENTITY_UNIT_2].player = 2;
TS_ASSERT_UNEVAL_EQUALS(cmpGuiInterface.GetEntityState(1, TEST_ENTITY_UNIT_2), {
	"dynamicState": expectedDynamicStates[TEST_ENTITY_UNIT_2],
	"templateData": { ...expectedTemplateData.example_unit_template, "maxHitpoints": 100 },
	"modificationsID": "Health/Max:IncreasedMaxHP(1) ",
	// modifiedTemplateData for example_unit_template and the above modificationsID has been returned in the previous
	// call, but for a different owner (player 1, not player 2), so it should be computed again now.
	"modifiedTemplateData": { "maxHitpoints": 150 }
});
