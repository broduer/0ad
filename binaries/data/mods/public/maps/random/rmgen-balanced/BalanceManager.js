/**
 * BalanceManager keeps information of player data, static constraints, and memoizes what it can.
 * Player IDs are 0-indexed for convenience.
 */
class BalanceManager
{
	constructor(playerIds, playerPosition)
	{
		this.playerPosition = [];
		for (let i = 0; i < getNumPlayers(); i++)
			this.playerPosition[playerIds[i] - 1] = playerPosition[i];

		this.partitionedConstraints = [];
		for (let i = 0; i < getNumPlayers(); i++)
			this.partitionedConstraints[i] = new StaticConstraint(new PlayerPartitionedConstraint(i, this.playerPosition));
	}

	/**
	 * Constraints defined here are constraint that apply for all objects. They
	 * are used to restrict layered areas, which allows a better estimate of
	 * ressource quantities from density, and may also lead to better
	 * performance. Constraints are further divided due to whether or not we
	 * wish to memoize the resulting area restriction.
	 */
	createObjectGroupManager(minDistance, maxDistance, layerSize, resourceDensity, memoizedConstraintsKey, memoizedConstraints = new NullConstraint(), additionalConstraints = new NullConstraint())
	{
		return new ObjectGroupManager(this, [memoizedConstraints, additionalConstraints], this.getOrCreateLayeredAreas(minDistance, maxDistance, layerSize, memoizedConstraintsKey, memoizedConstraints, additionalConstraints), this.getNeutralArea(maxDistance, new UncontestedAreaConstraint(20, this.playerPosition), [memoizedConstraints, additionalConstraints]), resourceDensity);
	}

	getOrCreateLayeredAreas(minDistance, maxDistance, layerSize, memoizedConstraintsKey, memoizedConstraints, additionalConstraints)
	{
		const playerLayeredAreas = [];
		for (let i = 0; i < getNumPlayers(); i++)
		{
			const constraint = new AndConstraint([memoizedConstraints, additionalConstraints, this.getStaticConstraints(i), new UncontestedAreaConstraint(20, this.playerPosition)]);
			const center = this.playerPosition[i];
			const numLayers = Math.floor((maxDistance - minDistance) / layerSize);
			const xMin = Math.floor(Math.max(0, center.x - maxDistance));
			const yMin = Math.floor(Math.max(0, center.y - maxDistance));
			const xMax = Math.ceil(Math.min(g_Map.getSize() - 1, center.x + maxDistance));
			const yMax = Math.ceil(Math.min(g_Map.getSize() - 1, center.y + maxDistance));

			const layers = new Array(numLayers).fill(0).map(_ => []);
			const it = new Vector2D();
			for (it.x = xMin; it.x <= xMax; ++it.x)
				for (it.y = yMin; it.y <= yMax; ++it.y)
				{
					const layer = Math.floor((center.distanceTo(it) - minDistance) / layerSize);
					if (layer >= 0 && layer < numLayers && constraint.allows(it))
						layers[layer].push(it.clone());
				}

			playerLayeredAreas[i] = layers.map(points => new Area(points));
		}

		return playerLayeredAreas;
	}

	getNeutralArea(playerDistance, uncontestedAreaConstraint, constraints)
	{
		const xMin = 0;
		const yMin = 0;
		const xMax = g_Map.getSize() - 1;
		const yMax = g_Map.getSize() - 1;
		const distanceSquared = Math.square(playerDistance);
		const constraint = new AndConstraint(constraints);
		const points = [];

		const it = new Vector2D();
		for (it.x = xMin; it.x <= xMax; ++it.x)
			for (it.y = yMin; it.y <= yMax; ++it.y)
			{
				if (constraint.allows(it) &&
					(!uncontestedAreaConstraint.allows(it) || this.playerPosition.every((pos) => pos.distanceToSquared(it) >= distanceSquared)))
					points.push(it.clone());
			}

		return new Area(points);
	}

	getStaticConstraints(playerId)
	{
		return this.partitionedConstraints[playerId];
	}
}
