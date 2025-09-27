

class ObjectGroupManager
{
	constructor(balanceManager, constraints, playerLayeredAreas, neutralArea, resourceDensity)
	{
		this.balanceManager = balanceManager;
		this.constraints = constraints;
		this.playerLayeredAreas = playerLayeredAreas;
		this.neutralArea = neutralArea;
		this.resourceDensity = resourceDensity;

		// this.resourceObjects = [];
		this.resourceObjects = {};
	}

	addResourceObject(resourceObject, category = "none")
	{
		// TODO: rewrite with if branch logic (no reassigning every time)
		const resourceObjectsInCategory = this.resourceObjects[category] ?? [];
		resourceObjectsInCategory.push(resourceObject);
		this.resourceObjects[category] = resourceObjectsInCategory;
	}

	place(tileClass)
	{
		for (let i = 0; i < this.playerLayeredAreas[0].length; i++)
		{
			const areas = this.playerLayeredAreas.map(p => p[i]);
			const areasSizes = areas.map(a => a.getPoints().length);
			const meanSizeArea = areasSizes.reduce((a, b) => a + b) / getNumPlayers();

			if (areasSizes.some(size => size < meanSizeArea / 4))
			{
				// Switching to team balance for this layer
				const teamMergedAreas = getTeamsArray().map((team) =>
					new Area(new MapBoundsPlacer().place(new StayAreasConstraint(areas.filter((_, id) => team.includes(id))))));
				const teamAreasSizes = teamMergedAreas.map(a => a.getPoints().length);
				const teamMeanSizeArea = teamAreasSizes.reduce((a, b) => a + b) / teamMergedAreas.length;
				if (teamAreasSizes.some(size => size < teamMeanSizeArea / 4))
					continue;

				const targetResourceCount = stepUniformPick(Math.floor(teamMeanSizeArea * this.resourceDensity), 8);
				const stepVariance = 0.05 * targetResourceCount;
				const maxVariance = 0.2 * targetResourceCount;
				for (const area of teamMergedAreas)
					new ResourcePainter(this.resourceObjects, tileClass, this.constraints, targetResourceCount, stepVariance, maxVariance).paint(area);
				continue;
			}

			const targetResourceCount = stepUniformPick(Math.floor(meanSizeArea * this.resourceDensity), 8);
			const stepVariance = 0.05 * targetResourceCount;
			const maxVariance = 0.2 * targetResourceCount;
			for (const area of areas)
				new ResourcePainter(this.resourceObjects, tileClass, this.constraints, targetResourceCount, stepVariance, maxVariance).paint(area);
		}

		new ResourceDensityPainter(this.resourceObjects, tileClass, this.constraints, this.resourceDensity, 0.2, 0.4).paint(this.neutralArea);
	}

	colorLayers()
	{
		new TerrainPainter("whiteness").paint(this.neutralArea);
		for (const layeredAreas of this.playerLayeredAreas)
			debugColorAreas(layeredAreas);
	}
}
