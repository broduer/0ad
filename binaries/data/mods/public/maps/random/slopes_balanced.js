Engine.LoadLibrary("rmgen");
Engine.LoadLibrary("rmgen-common");
Engine.LoadLibrary("rmgen-balanced");
Engine.LoadLibrary("rmbiome");

export function* generateMap(mapSettings) {

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
    const tWater = g_Terrains.water;
    const tShore = g_Terrains.shore;

    const oTree1 = g_Gaia.tree1;
    const oTree2 = g_Gaia.tree2;
    const oTree3 = g_Gaia.tree3;
    const oTree4 = g_Gaia.tree4;
    const oTree5 = g_Gaia.tree5;
    const oFish = g_Gaia.fish;
    const oFruitBush = g_Gaia.fruitBush;
    const oMainHuntableAnimal = g_Gaia.mainHuntableAnimal;
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

    const pForest1 = [tForestFloor2 + TERRAIN_SEPARATOR + oTree1, tForestFloor2 + TERRAIN_SEPARATOR + oTree2, tForestFloor2];
    const pForest2 = [tForestFloor1 + TERRAIN_SEPARATOR + oTree4, tForestFloor1 + TERRAIN_SEPARATOR + oTree5, tForestFloor1];

    const terrainSet = [tMainTerrain, tForestFloor1, tForestFloor2, pForest1, pForest2];

    const stragglerBerriesPresent = Engine.GetTemplate(oTree3).ResourceSupply.Type == "food.fruit";

    const heightLand = 0;

    globalThis.g_Map = new RandomMap(heightLand, tMainTerrain);
    const center = g_Map.getCenter();
    const mapSize = g_Map.getSize();
    const halfMapSize = g_Map.getSize() / 2;

    const numPlayers = getNumPlayers();

    const clPlayer = g_Map.createTileClass();
    const clHill = g_Map.createTileClass();
    const clForest = g_Map.createTileClass();
    const clDirt = g_Map.createTileClass();
    const clRock = g_Map.createTileClass();
    const clMetal = g_Map.createTileClass();
    const clFood = g_Map.createTileClass();
    const clFish = g_Map.createTileClass();
    const clBaseResource = g_Map.createTileClass();
    const clRamp = g_Map.createTileClass();
    const clWater = g_Map.createTileClass();
    const clShore = g_Map.createTileClass();
    const clHunt = g_Map.createTileClass();
    const clLeftPlateau = g_Map.createTileClass();
    const clRightPlateau = g_Map.createTileClass();
    let pattern = g_MapSettings.PlayerPlacement;
    const teams = getTeamsArray();
    if (teams.length != 2 && !mapSettings.Nomad) {
        throw new Error("Too many teams for slopes, use two teams.");
    }

    const startAngle = {
        "river": 1.600+Math.PI/2,
        "stronghold": 1.600
    }[pattern];

    const teamDist = {
        "river": 0.28,
        "stronghold": 0.34
    }[pattern];

    const playerDist = {
        "river": 0.5,
        "stronghold": 0.11
    }[pattern];

	const { playerIDs, playerPosition } =
        playerPlacementByPattern(
            pattern,
            fractionToTiles(teamDist),
            fractionToTiles(playerDist),
            startAngle,
            undefined);

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
            "count": 5
        },
        "Decoratives": {
        "template": aGrassShort
        }
    });

    yield 10;
    
	var numTeams = teams.filter(team => team).length;

	const balanceManager = new BalanceManager(playerIDs, playerPosition);

	let playerAreas;
	if (!mapSettings.Nomad)
	{
		playerAreas = getSurroundingAreas(playerPosition);
	}

    const heightTop = heightLand + 5;
    const lowlandsWidth = fractionToTiles(0.36);
    const rampWidth = fractionToTiles(0.05);
    const elevationPainter = new ElevationPainter(heightTop);

    const rightRampStartX = halfMapSize + lowlandsWidth / 2;
    const rightRampEndX = rightRampStartX + rampWidth;
    const leftRampStartX = halfMapSize - lowlandsWidth / 2;
    const leftRampEndX = leftRampStartX - rampWidth;

    const rightPlateauPlacer = new RectPlacer(new Vector2D(rightRampEndX, mapSize), new Vector2D(mapSize, 0));
    const leftPlateauPlacer = new RectPlacer(new Vector2D(0, mapSize), new Vector2D(leftRampEndX, 0));

    createArea(rightPlateauPlacer, [elevationPainter, new TileClassPainter(clRightPlateau)]);
    yield 12;
    createArea(leftPlateauPlacer, [elevationPainter, new TileClassPainter(clLeftPlateau)]);
    yield 13;

    createPassage({
      "start": new Vector2D(rightRampStartX, halfMapSize),
      "end": new Vector2D(rightRampEndX, halfMapSize),
      "startWidth": mapSize,
      "endWidth": mapSize,
      "smoothWidth": 2,
      "tileClass": clRamp,
      "terrain": tCliff,
      "edgeTerrain": tHill
    });
    yield 16;

    createPassage({
      "start": new Vector2D(leftRampStartX, halfMapSize),
      "end": new Vector2D(leftRampEndX, halfMapSize),
      "startWidth": mapSize,
      "endWidth": mapSize,
      "smoothWidth": 2,
      "tileClass": clRamp,
      "terrain": tCliff,
      "edgeTerrain": tHill
    });
    yield 18;

    const heightWaterLevel = heightTop - 7;

    const fish = new SimpleGroup([new SimpleObject(oFish, 2, 2, 0, 2)], true, clFish);
    const stone = new SimpleGroup(
      [new SimpleObject(oStoneLarge, 1, 1, 0, 4, 0, 2 * Math.PI, 4)],
      true,
      clRock
    );
    const metal = new SimpleGroup(
      [new SimpleObject(oMetalLarge, 1, 1, 0, 4)],
      true,
      clMetal
    );

    let annulusAreas = {};

    // Memoize areas
    function getAnnulusArea(minTileBound, maxTileBound, position) {
        const key = [minTileBound, maxTileBound, position.x, position.y];

        if (key in annulusAreas) {
            return annulusAreas[key];
        } else {
            const area = new Area(new AnnulusPlacer(minTileBound, maxTileBound, position).place());
            annulusAreas[key] = area;
            return area;
        }
    }
    function createSideLakes(xDistance, yDistance) {
      const stoneRight = randBool();
      let mineralDistribution = [stoneRight, !stoneRight];

      for (let x of [halfMapSize + xDistance, halfMapSize - xDistance]) {
        const position = new Vector2D(x, halfMapSize + yDistance);
        const lakeSize = scaleByMapSize(14, 38);
        const placer = new ChainPlacer(
          2,
          Math.floor(scaleByMapSize(3, 8)),
          Math.floor(scaleByMapSize(8, 20)),
          Infinity,
          position,
          0,
          [lakeSize]
        );

        createArea(
          placer,
          [
            new SmoothElevationPainter(ELEVATION_SET, heightWaterLevel - 3, 4),
            new TileClassPainter(clWater),
          ],
          new NullConstraint()
        );

        createObjectGroupsByAreas(fish, 0,
          avoidClasses(clFish, 6),
          scaleByMapSize(3, 9), 1000,
          [new Area(placer.place(stayClasses(clWater, 1)))]
        );

        const lowerRadius = lakeSize + 2;
        const maxRadius = lakeSize + 4;

        createObjectGroupsByAreas(mineralDistribution.pop() ? stone : metal, 0,
          avoidClasses(clWater, 4, clPlayer, 50),
          1, 400, [getAnnulusArea(lowerRadius, maxRadius, position)]
        );
      }
    }

    const lakeDistance = rampWidth + lowlandsWidth / 2 + fractionToTiles(0.11);

    createSideLakes(lakeDistance, fractionToTiles(0.31));
    yield 22;
    createSideLakes(lakeDistance, -fractionToTiles(0.31));
    yield 23;

    createArea(
      leftPlateauPlacer,
      new TerrainPainter(tWater),
      new HeightConstraint(-Infinity, heightTop - 1.5));
    yield 26;
    createArea(
      leftPlateauPlacer,
      new TerrainPainter(tShore),
      new HeightConstraint(heightTop - 1, heightTop - 2),
      new TileClassPainter(clShore)
    );
    yield 27;

    createArea(
      rightPlateauPlacer,
      new TerrainPainter(tWater),
      new HeightConstraint(-Infinity, heightTop - 1.5));
    yield 28;
    createArea(
      rightPlateauPlacer,
      new TerrainPainter(tShore),
      new HeightConstraint(heightTop - 1, heightTop - 2),
      new TileClassPainter(clShore)
    );
    yield 29;

    createBumps(avoidClasses(clPlayer, 20));

    yield 30;

    const mineralPlacer = new DiskPlacer(4, center);
    const centerMineralsArea = new Area(mineralPlacer.place(new NullConstraint()));

    createObjectGroupsByAreas(stone, 0,
      new NullConstraint(),
      1, 400, [centerMineralsArea]
    );

    createObjectGroupsByAreas(metal, 0,
      avoidClasses(clRock, 6),
      1, 400, [centerMineralsArea]
    );

    yield 32;

    if (randBool())
      createHills([tCliff, tCliff, tHill], avoidClasses(clPlayer, 35, clHill, 15, clMetal, 4, clRock, 4, clWater, 8), clHill, scaleByMapSize(2, 11));
    else
      createMountains(tCliff, avoidClasses(clPlayer, 35, clHill, 15, clMetal, 4, clRock, 4, clWater, 8), clHill, scaleByMapSize(2, 11));

    yield 34;

    if (!mapSettings.Nomad) {
        let numTrees = scaleByMapArea(30, 200, g_Map.getArea(128), g_Map.getArea(512));
        numTrees *= g_ResourceCounts.woodAvailability;
        const auxConstraint = avoidClasses(clPlayer, 20, clForest, 18, clHill, 4, clWater, 4);
        const constraint = new AndConstraint([auxConstraint, new AvoidMapBoundsConstraint(7)]);
        placePlayerWoodBalanced(playerAreas, terrainSet, constraint, clForest, numTrees, 0.2);
    }

    yield 44;

    const [forestTreesR, stragglerTreesR] = getTreeCounts(...rBiomeTreeCount(0.8));
    const [forestTreesL, stragglerTreesL] = getTreeCounts(...rBiomeTreeCount(0.8));
    const [forestTreesM, stragglerTrees] = getTreeCounts(...rBiomeTreeCount(0.4));

    g_DefaultNumberOfForests = scaleByMapSize(3, 15);
    createDefaultForests(
        terrainSet,
        [avoidClasses(clPlayer, 38, clForest, 18, clHill, 0, clWater, 3), stayClasses(clLeftPlateau, 0)],
        clForest,
        forestTreesL);

    yield 48;

    createDefaultForests(
        terrainSet,
        [avoidClasses(clPlayer, 38, clForest, 18, clHill, 0, clWater, 3),stayClasses(clRightPlateau, 0)],
        clForest,
        forestTreesR);

    yield 52;

    createDefaultForests(
        terrainSet,
        avoidClasses(clPlayer, 38, clForest, 24, clHill, 0, clRightPlateau, 10, clLeftPlateau, 10),
        clForest,
        forestTreesM);
    yield 55;

    g_Map.log("Creating dirt patches");
    createLayeredPatches(
      [scaleByMapSize(3, 6), scaleByMapSize(5, 10), scaleByMapSize(8, 21)],
      [[tMainTerrain,tTier1Terrain],[tTier1Terrain,tTier2Terrain], [tTier2Terrain,tTier3Terrain]],
      [1, 1],
      avoidClasses(clForest, 0, clHill, 0, clDirt, 5, clPlayer, 12, clWater, 3),
      scaleByMapSize(15, 45),
      clDirt);

    yield 58;

    g_Map.log("Creating grass patches");
    createPatches(
      [scaleByMapSize(2, 4), scaleByMapSize(3, 7), scaleByMapSize(5, 15)],
      tTier4Terrain,
      avoidClasses(clForest, 0, clHill, 0, clDirt, 5, clPlayer, 12, clWater, 3, clRamp, 0),
      scaleByMapSize(15, 45),
      clDirt);
    yield 59;

    g_Map.log("Creating stone mines");
    createMines(
      [
        [new SimpleObject(oStoneLarge, 1, 1, 0, 4, 0, 2 * Math.PI, 4)]
      ],
      avoidClasses(clForest, 1, clPlayer, 60, clRock, 22, clHill, 1, clWater, 30),
      clRock,
      scaleByMapSize(4, 16) - 2
    );
    yield 62;

    g_Map.log("Creating metal mines");
    createMines(
      [
        [new SimpleObject(oMetalLarge, 1, 1, 0, 4)]
      ],
      avoidClasses(clForest, 1, clPlayer, 60, clMetal, 22, clRock, 5, clHill, 1, clWater, 30),
      clMetal,
      scaleByMapSize(4, 16) - 2
    );

    yield 65;

    var planetm = 1;

    if (currentBiome() == "mainland_balanced/tropic")
      planetm = 8;

    createDecoration(
      [
        [new SimpleObject(aRockMedium, 1, 3, 0, 1)],
        [new SimpleObject(aRockLarge, 1, 2, 0, 1), new SimpleObject(aRockMedium, 1, 3, 0, 2)],
        [new SimpleObject(aGrassShort, 1, 2, 0, 1)],
        [new SimpleObject(aGrass, 2, 4, 0, 1.8), new SimpleObject(aGrassShort, 3,6, 1.2, 2.5)],
        [new SimpleObject(aBushMedium, 1, 2, 0, 2), new SimpleObject(aBushSmall, 2, 4, 0, 2)]
      ],
      [
        scaleByMapSize(16, 262),
        scaleByMapSize(8, 131),
        planetm * scaleByMapSize(13, 200),
        planetm * scaleByMapSize(13, 200),
        planetm * scaleByMapSize(13, 200)
      ],
      avoidClasses(clForest, 0, clPlayer, 0, clHill, 0, clWater, 0));

    yield 75;

    g_Map.log("Creating sparse hills stone mines");
    createBalancedStoneMines(
        oStoneSmall,
        oStoneSmall,
        clRock,
        [stayClasses(clLeftPlateau, 8), avoidClasses(clForest, 1, clPlayer, 38, clHill, 1, clRock, 40, clWater, 4)]
    );


    g_Map.log("Creating sparse hills metal mines");
    createBalancedStoneMines(
        oMetalSmall,
        oMetalSmall,
        clMetal,
        [stayClasses(clLeftPlateau, 8), avoidClasses(clForest, 2, clPlayer, 38, clHill, 1, clMetal, 40, clRock, 10, clWater, 4)]
    );

    g_Map.log("Creating sparse hills stone mines");
    createBalancedStoneMines(
        oStoneSmall,
        oStoneSmall,
        clRock,
        [stayClasses(clRightPlateau, 8), avoidClasses(clForest, 1, clPlayer, 38, clHill, 1, clRock, 40, clWater, 4)]
    );


    g_Map.log("Creating sparse hills metal mines");
    createBalancedStoneMines(
        oMetalSmall,
        oMetalSmall,
        clMetal,
        [stayClasses(clRightPlateau, 8), avoidClasses(clForest, 1, clPlayer, 38, clHill, 1, clMetal, 40, clRock, 10, clWater, 4)]
    );

    yield 78;

    if (!mapSettings.Nomad)
	{
		const oBerryStraggler = stragglerBerriesPresent ? oTree3 : null;

		placePlayerFoodBalanced(playerAreas, oFruitBush, oBerryStraggler, oMainHuntableAnimal, oSecondaryHuntableAnimal, clFood,
			avoidClasses(clForest, 2, clPlayer, 25, clHill, 1, clMetal, 4, clRock, 4, clFood, 15), g_ResourceCounts.huntBerryRatio ?? 0.5);

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

	yield 79;

    createFood(
      [
        [new SimpleObject(oFruitBush, 5, 7, 0, 4)]
      ],
      [
        2 * numPlayers
      ],
      avoidClasses(clForest, 0, clPlayer, 45, clHill, 1, clMetal, 4, clRock, 4, clFood, 10, clWater, 14),
      clFood);

    createFood(
      [
          [new SimpleObject(oMainHuntableAnimal, 5, 7, 0, 4)]
      ],
      [
          50*numPlayers
      ],
      [avoidClasses(clForest, 0, clPlayer, 50, clHill, 1, clMetal, 1, clRock, 1, clHunt, 65, clWater, 0), borderClasses(clWater, 0, 18)],
      clHunt);

    yield 80;

    createStragglerTrees(
      [oTree1, oTree2, oTree4, oTree3],
      avoidClasses(
        clForest, 8, clHill, 1, clPlayer,
        (currentBiome() == "mainland_balanced/savanna") ? 12 : 30,
        clMetal, 6, clRock, 6, clFood, 1, clWater, 4
      ),
      clForest,
      (currentBiome() == "mainland_balanced/savanna") ? stragglerTrees * 1.5 : stragglerTrees);

    yield 85;

    placePlayersNomad(clPlayer, avoidClasses(clForest, 1, clMetal, 4, clRock, 4, clHill, 4, clFood, 2, clWater, 6));

    return g_Map;
}
