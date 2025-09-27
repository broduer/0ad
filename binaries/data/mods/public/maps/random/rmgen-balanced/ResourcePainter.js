/**
 * TODO: extend to be more generic than using the resource of a template. For
 * example, could use the number of entities e.g. for bonus gaia stuff.
 * Consider embedding constraints and tileclass with the resourceObjects
 * parameters, for finer control.
 */
function ResourcePainter(resourceObjects, tileClass, constraints, baseResourceSupply, stepVariance, maxVariance, wideningThreshold = 10)
{
	this.resourceObjects = resourceObjects;
	this.tileClass = tileClass;
	this.constraints = constraints;
	this.baseResourceSupply = baseResourceSupply;
	this.stepVariance = stepVariance;
	this.maxVariance = maxVariance;
	this.wideningThreshold = wideningThreshold;
}

ResourcePainter.prototype.paint = function(area)
{
	for (let variance = this.stepVariance; variance <= this.maxVariance; variance += this.stepVariance)
		for (let i = 0; i < this.wideningThreshold; i++)
		{
			const groups = [];
			let resourceCount = 0;
			let consecutiveFails = 0;
			// TODO: instead of randomness, when fails, don't pick the same resourceObjectstype.
			while (consecutiveFails < 5)
			{
				const category = pickRandom(Object.keys(this.resourceObjects));
				const resourceObjectT = clone(pickRandom(this.resourceObjects[category]));
				const resourceObject = new ResourceObject(resourceObjectT.templateName, resourceObjectT.minCount, resourceObjectT.maxCount, resourceObjectT.minDistance, resourceObjectT.maxDistance);
				const placedResourceCount = resourceObject.pickObjectCount(this.baseResourceSupply - resourceCount);
				if (placedResourceCount)
				{
					consecutiveFails = 0;
					resourceCount += placedResourceCount;
					groups.push(new SimpleGroup([resourceObject], true, this.tileClass));
				}
				else
					consecutiveFails++;
			}

			if (this.baseResourceSupply - variance <= resourceCount && resourceCount <= this.baseResourceSupply + variance)
			{
				groups.forEach(group => createObjectGroupsByAreas(group, 0, this.constraints, 1, 300, [area]));
				return;
			}
		}
};
