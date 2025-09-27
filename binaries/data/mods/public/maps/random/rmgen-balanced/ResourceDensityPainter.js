
function ResourceDensityPainter(resourceObjects, tileClass, constraints, density, stepVarianceRelative, maxVarianceRelative, wideningThreshold = 10)
{
	this.resourceObjects = resourceObjects;
	this.tileClass = tileClass;
	this.constraints = constraints;
	this.density = density;
	this.stepVarianceRelative = stepVarianceRelative;
	this.maxVarianceRelative = maxVarianceRelative;
	this.wideningThreshold = wideningThreshold;
}

ResourceDensityPainter.prototype.paint = function(area)
{
	const targetResourceCount = this.density * area.getPoints().length;
	const stepVarianceAbsolute = this.stepVarianceRelative * targetResourceCount;
	const maxVarianceAbsolute = this.maxVarianceRelative * targetResourceCount;

	new ResourcePainter(this.resourceObjects, this.tileClass, this.constraints, targetResourceCount, stepVarianceAbsolute, maxVarianceAbsolute, this.wideningThreshold).paint(area);
};
