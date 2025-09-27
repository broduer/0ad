/**
 * Some of the functions in this file should be integrated in their own module.
 */

var colors = ["blackness", "blue", "neon green", "brown", "gray", "green", "purple", "light blue", "red", "yellow"];
var neutralColor = "whiteness";

function getSurroundingAreas(positions, radius = 35)
{
	return positions.map(pos => createArea(new DiskPlacer(radius, pos), null, null));
}

function getResourceSupply(templateName)
{
	return GetBaseTemplateDataValue(Engine.GetTemplate(templateName), "ResourceSupply/Max");
}

function AvoidMapBoundsConstraint(distance)
{
	this.maxDistanceToCenter = (g_Map.getSize() / 2) - distance;
}

AvoidMapBoundsConstraint.prototype.allows = function(position)
{
	return g_Map.getCenter().distanceTo(position) < this.maxDistanceToCenter;
};

/**
 * Get points from the given area which are far enough from the border.
 * @param points - The input list of points.
 * @param tc - an auxiliary tileclass which contains the same points as the given area.
 *        it is given externally as it may be built incrementally, for performance.
 * @param distance - How far away the points should be from the border.
 */
function interiorPointList(points, tc, distance)
{
	const constraint = new StayInTileClassConstraint(tc, distance);
	return points.filter(p => constraint.allows(p));
}


function debugColorAreas(areas)
{
	let colorIdx = 0;
	for (const area of areas)
	{
		new TerrainPainter(colors[colorIdx]).paint(area);
		colorIdx = (colorIdx + 1) % colors.length;
	}
}

function PlayerPartitionedConstraint(playerId, playerPositions)
{
	this.ownPosition = playerPositions[playerId];
	this.otherPositions = [...playerPositions];
	this.otherPositions.splice(playerId, 1);
}

PlayerPartitionedConstraint.prototype.allows = function(position)
{
	const ownDistanceSquared = position.distanceToSquared(this.ownPosition);
	for (const otherPosition of this.otherPositions)
		if (position.distanceToSquared(otherPosition) < ownDistanceSquared)
			return false;

	return true;
};

function UncontestedAreaConstraint(threshold, playerPositions)
{
	this.threshold = threshold;
	this.teamsPositions = getTeamsArray().map(team => team.map(id => playerPositions[id - 1]));
}

UncontestedAreaConstraint.prototype.allows = function(position)
{
	const [d1, d2] = this.teamsPositions
		.map(team => Math.min(...team.map(playerPosition => playerPosition.distanceTo(position))))
		.sort((a, b) => a - b);
	return (d2 - d1) > this.threshold;
};

/**
 * numSteps must be even for now.
 */
function stepUniformPick(mean, numSteps, lowRelative = 0)
{
	return pickRandom(Array(numSteps).fill().map((_, i) => 2 * i * mean / numSteps));
}
