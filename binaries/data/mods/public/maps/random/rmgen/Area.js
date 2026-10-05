/**
 * @file An Area is a set of Vector2D points and a cache to lookup membership quickly.
 */

function Area(points)
{
	this.points = deepfreeze(points);

	const mapSize = g_Map.getSize();

	this.cache = new Array(mapSize).fill(0).map(() => new Uint8Array(mapSize));
	for (const point of points)
		this.cache[point.x][point.y] = 1;
}

Area.prototype.getPoints = function()
{
	return this.points;
};

Area.prototype.contains = function(point)
{
	return g_Map.inMapBounds(point) && this.cache[point.x][point.y] == 1;
};

Area.prototype.getClosestPointTo = function(position)
{
	const points = this.points;
	if (!points.length)
		return undefined;

	let closestPoint = points[0];
	let shortestDistance = Infinity;

	for (let i = 0; i < points.length; ++i)
	{
		const point = points[i];
		const currentDistance = point.distanceToSquared(position);
		if (currentDistance < shortestDistance)
		{
			if (currentDistance === 0)
				return point;

			shortestDistance = currentDistance;
			closestPoint = point;
		}
	}

	return closestPoint;
};
