/**
 * Works similarly to the ChainPlacer, but instead of having a predefined number
 * of circles that are placed, the IncrementalChainPlacer will stop once a
 * condition has been reached on the current placed points.
 */
function IncrementalChainPlacer(minRadius, maxRadius, stopCondition, failFraction = 0, centerPosition = undefined, maxDistance = 0, queue = [], maxConsecutiveFail = 100)
{
	this.minRadius = minRadius;
	this.maxRadius = maxRadius;
	this.stopCondition = stopCondition;
	this.failFraction = failFraction;
	this.maxDistance = maxDistance;
	this.queue = queue.map(radius => Math.floor(radius));
	this.maxConsecutiveFail = maxConsecutiveFail;
	this.centerPosition = undefined;

	if (centerPosition)
		this.setCenterPosition(centerPosition);
}

IncrementalChainPlacer.prototype.setCenterPosition = function(position)
{
	this.centerPosition = deepfreeze(position.clone().round());
};

IncrementalChainPlacer.prototype.place = function(constraint)
{
	// Preliminary bounds check
	if (!g_Map.inMapBounds(this.centerPosition) || !constraint.allows(this.centerPosition))
		return undefined;

	const points = [];
	const auxTc = g_Map.createTileClass();
	let size = g_Map.getSize();
	let failed = 0;
	let consecutiveFailed = 0;
	let count = 0;

	const gotRet = new Array(size).fill(0).map(p => new Array(size).fill(-1));
	--size;

	this.minRadius = Math.min(this.maxRadius, Math.max(this.minRadius, 1));

	const edges = [this.centerPosition];

	while (!this.stopCondition(points, auxTc))
	{
		const chainPos = pickRandom(edges);
		const radius = this.queue.length ? this.queue.pop() : randIntInclusive(this.minRadius, this.maxRadius);
		const radius2 = Math.square(radius);

		const bbox = getPointsInBoundingBox(getBoundingBox([
			new Vector2D(Math.max(0, chainPos.x - radius), Math.max(0, chainPos.y - radius)),
			new Vector2D(Math.min(chainPos.x + radius, size), Math.min(chainPos.y + radius, size))
		]));

		for (const position of bbox)
		{
			if (position.distanceToSquared(chainPos) >= radius2)
				continue;

			++count;

			if (!g_Map.inMapBounds(position) || !constraint.allows(position))
			{
				++failed;
				++consecutiveFailed;
				if (consecutiveFailed >= this.maxConsecutiveFail)
					return undefined;

				continue;
			}

			const state = gotRet[position.x][position.y];
			if (state == -1)
			{
				consecutiveFailed = 0;
				points.push(position);
				auxTc.add(position);
				gotRet[position.x][position.y] = -2;
			}
			else if (state >= 0)
			{
				edges.splice(state, 1);
				gotRet[position.x][position.y] = -2;

				for (let k = state; k < edges.length; ++k)
					--gotRet[edges[k].x][edges[k].y];
			}
		}

		for (const pos of bbox)
		{
			if (this.maxDistance &&
			    (Math.abs(this.centerPosition.x - pos.x) > this.maxDistance ||
			     Math.abs(this.centerPosition.y - pos.y) > this.maxDistance))
				continue;

			if (gotRet[pos.x][pos.y] != -2)
				continue;

			if (pos.x > 0 && gotRet[pos.x - 1][pos.y] == -1 ||
			    pos.y > 0 && gotRet[pos.x][pos.y - 1] == -1 ||
			    pos.x < size && gotRet[pos.x + 1][pos.y] == -1 ||
			    pos.y < size && gotRet[pos.x][pos.y + 1] == -1)
			{
				edges.push(pos);
				gotRet[pos.x][pos.y] = edges.length - 1;
			}
		}
	}

	return failed > count * this.failFraction ? undefined : points;
};
