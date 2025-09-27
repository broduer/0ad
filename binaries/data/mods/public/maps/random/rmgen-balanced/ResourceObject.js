/**
 *
 */
function ResourceObject(templateName, minCount, maxCount, minDistance, maxDistance, minSupplyToRelax = Infinity, minAngle = 0, maxAngle = 2 * Math.PI, avoidDistance = 1)
{
	this.templateName = templateName;
	this.minCount = minCount;
	this.minCountRelaxed = Math.min(minCount, Math.ceil(minSupplyToRelax / this.getResourceSupply(1)));
	this.maxCount = maxCount;
	this.minDistance = minDistance;
	this.maxDistance = maxDistance;
	this.minAngle = minAngle;
	this.maxAngle = maxAngle;
	this.avoidDistance = avoidDistance;

	this.count = null;
}

ResourceObject.prototype.getResourceSupply = function(count = 1)
{
	return count * GetBaseTemplateDataValue(Engine.GetTemplate(this.templateName), "ResourceSupply/Max");
};

ResourceObject.prototype.pickObjectCount = function(maxResourceSupply, relaxMinCount = false)
{
	const countBeforeMaxSupply = Math.floor(maxResourceSupply / this.getResourceSupply());
	let minCount = (countBeforeMaxSupply < this.minCount && this.minCountRelaxed <= countBeforeMaxSupply) ? countBeforeMaxSupply : this.minCount;
	const maxCount = Math.min(countBeforeMaxSupply, this.maxCount);
	if (maxCount < minCount)
		return 0;
	if (countBeforeMaxSupply <= this.maxCount)
		minCount = maxCount;

	this.count = randIntInclusive(minCount, maxCount);
	return this.getResourceSupply(this.count);
};

ResourceObject.prototype.place = function(centerPosition, player, avoidPositions, constraint, maxRetries)
{
	return new SimpleObject(this.templateName, this.count, this.count, this.minDistance, this.maxDistance, this.minAngle, this.maxAngle, this.avoidDistance).place(
		centerPosition, player, avoidPositions, constraint, maxRetries);
};
