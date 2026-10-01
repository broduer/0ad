/**
 * Applies smoothing to the given area using Inverse-Distance-Weighting / Shepard's method.
 *
 * @param {Number} size - Determines the number of neighboring heights to interpolate. The area is a square with the length twice this size.
 * @param {Number} strength - Between 0 (no effect) and 1 (only neighbor heights count). This parameter has the lowest performance impact.
 * @param {Number} iterations - How often the process should be repeated. Typically 1. Can be used to gain even more smoothing.
 */
function SmoothingPainter(size, strength, iterations)
{
	if (size < 1)
		throw new Error("Invalid size: " + size);

	if (strength <= 0 || strength > 1)
		throw new Error("Invalid strength: " + strength);

	if (iterations <= 0)
		throw new Error("Invalid iterations: " + iterations);

	this.size = Math.floor(size);
	this.strength = strength;
	this.iterations = iterations;
}

SmoothingPainter.prototype.paint = function(area)
{
	const brushPoints = getPointsInBoundingBox(getBoundingBox(
		new Array(2).fill(0).map((zero, i) => new Vector2D(1, 1).mult(this.size).mult(i ? 1 : -1))));

	// Offsets of the neighbors and their distances, excluding the center vertex.
	const neighbors = brushPoints.filter(brushPoint => brushPoint.x || brushPoint.y);
	const neighborX = Int32Array.from(neighbors, brushPoint => brushPoint.x);
	const neighborY = Int32Array.from(neighbors, brushPoint => brushPoint.y);
	const neighborDistance = Float64Array.from(neighbors, brushPoint => Math.abs(brushPoint.x) + Math.abs(brushPoint.y));

	const height = g_Map.height;
	const vertexSize = height.length;

	// Additional complexity to process all 4 vertices of each tile, i.e the last row too
	const seen = new Uint8Array(vertexSize * vertexSize);

	for (let i = 0; i < this.iterations; ++i)
	{
		seen.fill(0);

		for (const point of area.getPoints())
			for (const tileVertex of g_TileVertices)
			{
				const x = point.x + tileVertex.x;
				const y = point.y + tileVertex.y;
				if (x < 0 || y < 0 || x >= vertexSize || y >= vertexSize || seen[x * vertexSize + y])
					continue;

				seen[x * vertexSize + y] = 1;

				let sumWeightedHeights = 0;
				let sumWeights = 0;

				for (let k = 0; k < neighborDistance.length; ++k)
				{
					const xx = x + neighborX[k];
					const yy = y + neighborY[k];
					if (xx < 0 || yy < 0 || xx >= vertexSize || yy >= vertexSize)
						continue;

					sumWeightedHeights += height[xx][yy] / neighborDistance[k];
					sumWeights += 1 / neighborDistance[k];
				}

				height[x][y] =
					this.strength * sumWeightedHeights / sumWeights +
					(1 - this.strength) * height[x][y];
			}
	}
};
