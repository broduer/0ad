function RallyPoint() {}

RallyPoint.prototype.Schema =
	"<a:component/><empty/>";

RallyPoint.prototype.Init = function()
{
	this.perPlayer = {};
};

RallyPoint.prototype.GetOwner = function()
{
	return Engine.QueryInterface(this.entity, IID_Ownership)?.GetOwner();
};

RallyPoint.prototype.AddPosition = function(x, z, player = this.GetOwner())
{
	if (!this.perPlayer[player])
		this.perPlayer[player] = { "pos": [], "data": [] };
	this.perPlayer[player].pos.push({ "x": x, "z": z });
};

RallyPoint.prototype.HasPositions = function(player = this.GetOwner())
{
	return !!this.perPlayer[player]?.pos.length;
};

RallyPoint.prototype.GetFirstPosition = function()
{
	const pos = this.perPlayer[this.GetOwner()]?.pos;
	return pos?.length ? Vector2D.from3D(pos[0]) : new Vector2D(-1, -1);
};

RallyPoint.prototype.GetPositions = function(player = this.GetOwner())
{
	const playerEntry = this.perPlayer[player];
	if (!playerEntry)
		return [];

	const cmpRangeManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_RangeManager);

	// We must not affect the simulation state here, so copy the state
	const ret = [];
	for (let i = 0; i < playerEntry.pos.length; i++)
	{
		ret.push(playerEntry.pos[i]);

		// Update the rallypoint coordinates if the target is alive
		if (!playerEntry.data[i]?.target || !this.TargetIsAlive(playerEntry.data[i].target))
			continue;

		// and visible to the player who set this rally point
		if (cmpRangeManager &&
				cmpRangeManager.GetLosVisibility(playerEntry.data[i].target, player) != "visible")
			continue;

		// Get the actual position of the target entity
		const cmpPosition = Engine.QueryInterface(playerEntry.data[i].target, IID_Position);
		if (!cmpPosition?.IsInWorld())
			continue;

		const targetPosition = cmpPosition.GetPosition2D();
		if (!targetPosition)
			continue;

		if (playerEntry.pos[i].x == targetPosition.x && playerEntry.pos[i].z == targetPosition.y)
			continue;

		ret[i] = { "x": targetPosition.x, "z": targetPosition.y };
	}

	return ret;
};

// Extra data for the rally point, should have a command property and then helpful data for that command
// See getActionInfo in gui/input.js
RallyPoint.prototype.AddData = function(data, player = this.GetOwner())
{
	if (!this.perPlayer[player])
		this.perPlayer[player] = { "pos": [], "data": [] };
	this.perPlayer[player].data.push(data);
};

// Returns an array with the data associated with this rally point.  Each element has the structure:
// {"type": "walk/gather/garrison/...", "target": targetEntityId, "resourceType": "tree/fruit/ore/..."} where target
// and resourceType (specific resource type) are optional, also target may be an invalid entity, check for existence.
RallyPoint.prototype.GetData = function(player = this.GetOwner())
{
	return this.perPlayer[player]?.data ?? [];
};

RallyPoint.prototype.Unset = function(player = this.GetOwner())
{
	delete this.perPlayer[player];
};

/**
 * @param {number} entity - The entity ID of the entity to order to the rally point.
 * @param {string[]} ignore - The commands to ignore when performed on this.entity.
 *				E.g. "garrison" when unloading.
 */
RallyPoint.prototype.OrderToRallyPoint = function(entity, ignore = [])
{
	const cmpEntOwnership = Engine.QueryInterface(entity, IID_Ownership);
	if (!cmpEntOwnership)
		return;
	const entOwner = cmpEntOwnership.GetOwner();

	if (!this.HasPositions(entOwner))
		return;

	const playerEntry = this.perPlayer[entOwner];
	const commands = GetRallyPointCommands(playerEntry.pos, playerEntry.data, [entity]);
	if (!commands.length ||
		commands[0].target == this.entity && ignore.includes(commands[0].type))
		return;

	for (const command of commands)
		ProcessCommand(entOwner, command);
};

RallyPoint.prototype.OnGlobalEntityRenamed = function(msg)
{
	for (const playerEntry of Object.values(this.perPlayer))
		for (const data of playerEntry.data)
		{
			if (data?.target == msg.entity)
				data.target = msg.newentity;
			if (data?.source == msg.entity)
				data.source = msg.newentity;
		}

	if (msg.entity != this.entity)
		return;

	const cmpRallyPointNew = Engine.QueryInterface(msg.newentity, IID_RallyPoint);
	if (cmpRallyPointNew)
		for (const player in this.perPlayer)
		{
			const playerEntry = this.perPlayer[player];
			for (let i = 0; i < playerEntry.pos.length; ++i)
			{
				cmpRallyPointNew.AddPosition(playerEntry.pos[i].x, playerEntry.pos[i].z, +player);
				cmpRallyPointNew.AddData(playerEntry.data[i], +player);
			}
		}
};

RallyPoint.prototype.OnOwnershipChanged = function(msg)
{
	// No need to reset when constructing or destructing the entity
	if (msg.from == INVALID_PLAYER || msg.to == INVALID_PLAYER)
		return;

	this.perPlayer = {};
};

/**
 * Returns true if the target exists and has non-zero hitpoints.
 */
RallyPoint.prototype.TargetIsAlive = function(ent)
{
	var cmpFormation = Engine.QueryInterface(ent, IID_Formation);
	if (cmpFormation)
		return true;

	var cmpHealth = QueryMiragedInterface(ent, IID_Health);
	return cmpHealth && cmpHealth.GetHitpoints() != 0;
};

Engine.RegisterComponentType(IID_RallyPoint, "RallyPoint", RallyPoint);
