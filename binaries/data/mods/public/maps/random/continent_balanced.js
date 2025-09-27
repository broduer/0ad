Engine.LoadLibrary("rmgen")
Engine.LoadLibrary("rmgen-common");
Engine.LoadLibrary("rmgen-balanced");
Engine.LoadLibrary("rmbiome");

export function* generateMap(mapSettings)
{
	setBiome(mapSettings.Biome);

	const tMainTerrain = g_Terrains.mainTerrain;
	const tForestFloor1 = g_Terrains.forestFloor1;
	const tForestFloor2 = g_Terrains.forestFloor2;
	const tCliff = g_Terrains.cliff;
	const tTier1Terrain = g_Terrains.tier1Terrain;
	const tTier2Terrain = g_Terrains.tier2Terrain;
	const tTier3Terrain = g_Terrains.tier3Terrain;
	const tHill = g_Terrains.hill;
	const tRoad = g_Terrains.road;
	const tRoadWild = g_Terrains.roadWild;
	const tTier4Terrain = g_Terrains.tier4Terrain;
	const tShore = g_Terrains.shore;
	const tWater = g_Terrains.water;

	const oTree1 = g_Gaia.tree1;
	const oTree2 = g_Gaia.tree2;
	const oTree3 = g_Gaia.tree3;
	const oTree4 = g_Gaia.tree4;
	const oTree5 = g_Gaia.tree5;
	const oFruitBush = g_Gaia.fruitBush;
	const oMainHuntableAnimal = g_Gaia.mainHuntableAnimal;
	const oFish = g_Gaia.fish;
	const oSecondaryHuntableAnimal = g_Gaia.secondaryHuntableAnimal;
	const oStoneLarge = g_Gaia.stoneLarge;
	const oStoneSmall = g_Gaia.stoneSmall;
	const oMetalLarge = g_Gaia.metalLarge;
	const oMetalSmall = g_Gaia.metalSmall;

	const aGrass = g_Decoratives.grass;
	const aGrassShort = g_Decoratives.grassShort;
	const aRockLarge = g_Decoratives.rockLarge;
	const aRockMedium = g_Decoratives.rockMedium;
	const aBushMedium = g_Decoratives.bushMedium;
	const aBushSmall = g_Decoratives.bushSmall;

	const pForest1 = [
		tForestFloor2 + TERRAIN_SEPARATOR + oTree1,
		tForestFloor2 + TERRAIN_SEPARATOR + oTree2,
		tForestFloor2
	];
	const pForest2 = [
		tForestFloor1 + TERRAIN_SEPARATOR + oTree4,
		tForestFloor1 + TERRAIN_SEPARATOR + oTree5,
		tForestFloor1
	];

	const terrainSet = [tMainTerrain, tForestFloor1, tForestFloor2, pForest1, pForest2];

	const stragglerBerriesPresent = Engine.GetTemplate(oTree3).ResourceSupply.Type == "food.fruit";

	const heightSeaGround = -5;
	const heightLand = 3;

	globalThis.g_Map = new RandomMap(heightSeaGround, tWater);

	const numPlayers = getNumPlayers();
	const mapSize = g_Map.getSize();
	const mapCenter = g_Map.getCenter();

	const clPlayer = g_Map.createTileClass();
	const clHill = g_Map.createTileClass();
	const clForest = g_Map.createTileClass();
	const clDirt = g_Map.createTileClass();
	const clRock = g_Map.createTileClass();
	const clMetal = g_Map.createTileClass();
	const clFood = g_Map.createTileClass();
	const clFish = g_Map.createTileClass();
	const clBaseResource = g_Map.createTileClass();
	const clLand = g_Map.createTileClass();

	g_Map.log("Creating continent");
	createArea(
		new ChainPlacer(
			2,
			Math.floor(scaleByMapSize(5, 12)),
			Math.floor(scaleByMapSize(60, 700)),
			Infinity,
			mapCenter,
			0,
			[Math.floor(fractionToTiles(0.33))]),
		[
			new SmoothElevationPainter(ELEVATION_SET, heightLand, 4),
			new TileClassPainter(clLand)
		]);

	const { playerIDs, playerPosition } =
		playerPlacementByPattern(
			mapSettings.PlayerPlacement,
			fractionToTiles(0.25),
			fractionToTiles(0.1),
			randomAngle(),
			undefined);

	g_Map.log("Ensuring initial player land");
	for (let i = 0; i < numPlayers; ++i)
		createArea(
			new ChainPlacer(
				2,
				Math.floor(scaleByMapSize(5, 9)),
				Math.floor(scaleByMapSize(5, 20)),
				Infinity,
				playerPosition[i],
				0,
				[Math.floor(scaleByMapSize(23, 50))]),
			[
				new SmoothElevationPainter(ELEVATION_SET, heightLand, 4),
				new TileClassPainter(clLand)
			]);

	yield 20;

	paintTerrainBasedOnHeight(3, 4, 3, tMainTerrain);
	paintTerrainBasedOnHeight(1, 3, 0, tShore);
	paintTerrainBasedOnHeight(-8, 1, 2, tWater);

	placePlayerBases({
		"PlayerPlacement": [playerIDs, playerPosition],
		"PlayerTileClass": clPlayer,
		"BaseResourceClass": clBaseResource,
		"CityPatch": {
			"outerTerrain": tRoadWild,
			"innerTerrain": tRoad
		},
		"StartingAnimal": {
		},
		"Berries": {
			"template": oFruitBush
		},
		"Mines": {
			"types": [
				{ "template": oMetalLarge },
				{ "template": oStoneLarge }
			]
		},
		"Trees": {
			"template": oTree1,
			"count": 2
		},
		"Decoratives": {
			"template": aGrassShort
		}
	});
	yield 30;

	var teams = getTeamsArray();
	var numTeams = teams.filter(team => team).length;

	const balanceManager = new BalanceManager(playerIDs, playerPosition);

	let playerAreas;
	if (!mapSettings.Nomad) {
		playerAreas = getSurroundingAreas(playerPosition);
	}

	createBumps([avoidClasses(clPlayer, 10), stayClasses(clLand, 5)]);

	if (randBool())
		createHills([tMainTerrain, tCliff, tHill],
			[avoidClasses(clPlayer, 20, clHill, 15, clBaseResource, 3), stayClasses(clLand, 5)],
			clHill,
			scaleByMapSize(1, 1) * Math.floor(numPlayers/2));
	else
		createMountains(tCliff,
			[avoidClasses(clPlayer, 20, clHill, 15, clBaseResource, 3), stayClasses(clLand, 5)],
			clHill,
			scaleByMapSize(1, 1) * Math.floor(numPlayers/2));

	if (!mapSettings.Nomad)
	{
		let numTrees = scaleByMapArea(30, 200, g_Map.getArea(128), g_Map.getArea(512));
		numTrees *= g_ResourceCounts.woodAvailability;
		const auxConstraint = avoidClasses(clPlayer, 20, clForest, 18, clHill, 0);
		const constraint = new AndConstraint([auxConstraint, stayClasses(clLand, 4), new AvoidMapBoundsConstraint(7)]);
		placePlayerWoodBalanced(playerAreas, terrainSet, constraint, clForest, numTrees, 0.2);
	}

	g_DefaultNumberOfForests = scaleByMapSize(6, 30);
	var [forestTrees, stragglerTrees] = getTreeCounts(...rBiomeTreeCount(1));
	createDefaultForests(
		terrainSet,
		[avoidClasses(clPlayer, 38, clForest, 18, clHill, 0),stayClasses(clLand, 4)],
		clForest,
		forestTrees);

	yield 50;

	g_Map.log("Creating dirt patches");
	createLayeredPatches(
		[scaleByMapSize(3, 6), scaleByMapSize(5, 10), scaleByMapSize(8, 21)],
		[[tMainTerrain, tTier1Terrain], [tTier1Terrain, tTier2Terrain], [tTier2Terrain, tTier3Terrain]],
		[1, 1],
		[avoidClasses(clForest, 0, clHill, 0, clDirt, 5, clPlayer, 12), stayClasses(clLand, 5)],
		scaleByMapSize(15, 45),
		clDirt);

	g_Map.log("Creating grass patches");
	createPatches(
		[scaleByMapSize(2, 4), scaleByMapSize(3, 7), scaleByMapSize(5, 15)],
		tTier4Terrain,
		[avoidClasses(clForest, 0, clHill, 0, clDirt, 5, clPlayer, 12), stayClasses(clLand, 5)],
		scaleByMapSize(15, 45),
		clDirt);
	yield 55;

	const metalConstraints = new AndConstraint([avoidClasses(clForest, 1, clHill, 1, clMetal, 7), stayClasses(clLand, 4), new PassableMapAreaConstraint()]);
	const metalGroupManager = balanceManager.createObjectGroupManager(25, 100, 15, 4, undefined, metalConstraints);

	metalGroupManager.addResourceObject(new ResourceObject(oMetalLarge, 1, 1, 0, 1));
	metalGroupManager.addResourceObject(new ResourceObject(oMetalSmall, 1, 3, 0, 2));
	metalGroupManager.place(clMetal);

	const stoneConstraints = new AndConstraint([avoidClasses(clForest, 1, clHill, 1, clMetal, 7, clRock, 7), stayClasses(clLand, 4), new PassableMapAreaConstraint()]);
	const stoneGroupManager = balanceManager.createObjectGroupManager(25, 100, 15, 3.5, undefined, stoneConstraints);

	stoneGroupManager.addResourceObject(new ResourceObject(oStoneLarge, 1, 1, 0, 1));
	stoneGroupManager.addResourceObject(new ResourceObject(oStoneSmall, 1, 3, 0, 2));
	stoneGroupManager.place(clRock);

	yield 65;

	// create decoration
	let planetm = 1;

	if (currentBiome() == "generic/india")
		planetm = 8;

	createDecoration(
		[
			[new SimpleObject(aRockMedium, 1, 3, 0, 1)],
			[new SimpleObject(aRockLarge, 1, 2, 0, 1), new SimpleObject(aRockMedium, 1, 3, 0, 2)],
			[new SimpleObject(aGrassShort, 1, 2, 0, 1)],
			[new SimpleObject(aGrass, 2, 4, 0, 1.8), new SimpleObject(aGrassShort, 3, 6, 1.2, 2.5)],
			[new SimpleObject(aBushMedium, 1, 2, 0, 2), new SimpleObject(aBushSmall, 2, 4, 0, 2)]
		],
		[
			scaleByMapAreaAbsolute(16),
			scaleByMapAreaAbsolute(8),
			planetm * scaleByMapAreaAbsolute(13),
			planetm * scaleByMapAreaAbsolute(13),
			planetm * scaleByMapAreaAbsolute(13)
		],
		[avoidClasses(clForest, 0, clPlayer, 0, clHill, 0), stayClasses(clLand, 5)]);

	yield 70;

	if (!mapSettings.Nomad)
	{
		const oBerryStraggler = stragglerBerriesPresent ? oTree3 : null;

		placePlayerFoodBalanced(playerAreas, oFruitBush, oBerryStraggler, oMainHuntableAnimal, oSecondaryHuntableAnimal, clFood,
			[avoidClasses(clForest, 2, clPlayer, 25, clHill, 1, clMetal, 4, clRock, 4, clFood, 15),stayClasses(clLand, 4)], g_ResourceCounts.huntBerryRatio ?? 0.5);

		if (oBerryStraggler)
		{
			const stragglerConstraints = avoidClasses(clForest, 8, clPlayer, 18, clHill, 1, clMetal, 4, clRock, 4, clFood, 12);
			const distribution = [0.3, 0.5, 0.7, 0.8, 0.9];
			let numStragglerBerries = 0;
			const rand = Math.random();
			while (numStragglerBerries < distribution.length)
			{
				if (rand < distribution[numStragglerBerries])
					break;

				numStragglerBerries++;
			}

			for (const area of playerAreas)
			{
				for (let i = 0; i < numStragglerBerries; i++)
				{
					const group = new SimpleGroup(
						[new SimpleObject(oBerryStraggler, 1, 1, 0, 4)],
						true, clFood
					);
					createObjectGroupsByAreas(group, 0, stragglerConstraints, 1, 100, [area]);
				}
			}
		}
	}

	createFood(
		[
			[new SimpleObject(oFish, 2, 3, 0, 2)]
		],
		[
			50 * numPlayers
		],
		avoidClasses(clLand, 2, clFish, 10),
		clFish);

	createFood(
		[
			[new SimpleObject(oMainHuntableAnimal, 5, 7, 0, 4)],
			[new SimpleObject(oSecondaryHuntableAnimal, 2, 3, 0, 2)]
		],
		[
			3 * numPlayers,
			3 * numPlayers
		],
		[avoidClasses(clForest, 0, clPlayer, 40, clHill, 1, clFood, 20), stayClasses(clLand, 5)],
		clFood);

	createFood(
		[
			[new SimpleObject(oFruitBush, 5, 7, 0, 4)]
		],
		[
			3 * numPlayers
		],
		[avoidClasses(clForest, 0, clPlayer, 45, clHill, 1, clFood, 10), stayClasses(clLand, 5)],
		clFood);

	yield 85;

	createStragglerTrees(
		[oTree1, oTree2, oTree4, oTree3],
		[
			avoidClasses(clForest, 7, clHill, 1, clPlayer, 9, clMetal, 6, clRock, 6),
			stayClasses(clLand, 7)
		],
		clForest,
		stragglerTrees);

	placePlayersNomad(
		clPlayer,
		[
			stayClasses(clLand, 4),
			avoidClasses(clForest, 1, clMetal, 4, clRock, 4, clHill, 4, clFood, 2)
		]);

	setWaterWaviness(1.0);
	setWaterType("ocean");

	return g_Map;
}
