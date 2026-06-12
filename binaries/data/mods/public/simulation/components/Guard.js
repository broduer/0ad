function Guard() {}

Guard.prototype.Schema =
	"<empty/>";

Guard.prototype.Init = function()
{
	this.entities = [];
	this.nextAttackedMessageTime = 0;
};

/**
 * Minimum time in milliseconds between MT_GuardedAttacked notifications.
 * Many attackers hitting the guarded entity in the same turns (e.g. dozens
 * of archers shooting a guarded structure) would otherwise send a message
 * to every guard for every single hit (#6007).
 */
Guard.prototype.NotificationInterval = 2000;

Guard.prototype.GetRange = function(entity)
{
	let range = 8;
	const cmpFootprint = Engine.QueryInterface(entity, IID_Footprint);
	if (cmpFootprint)
	{
		const shape = cmpFootprint.GetShape();
		if (shape.type == "square")
			range += Math.sqrt(shape.depth*shape.depth + shape.width*shape.width)*2/3;
		else if (shape.type == "circle")
			range += shape.radius*3/2;
	}
	return range;
};

Guard.prototype.GetEntities = function()
{
	return this.entities.slice();
};

Guard.prototype.SetEntities = function(entities)
{
	this.entities = entities;
};

Guard.prototype.AddGuard = function(ent)
{
	if (this.entities.indexOf(ent) != -1)
		return;
	this.entities.push(ent);
	// Let the new guard be notified of an ongoing attack without delay.
	this.nextAttackedMessageTime = 0;
};

Guard.prototype.RemoveGuard = function(ent)
{
	const index = this.entities.indexOf(ent);
	if (index != -1)
		this.entities.splice(index, 1);
};

Guard.prototype.RenameGuard = function(oldent, newent)
{
	const index = this.entities.indexOf(oldent);
	if (index != -1)
		this.entities[index] = newent;
};

Guard.prototype.OnAttacked = function(msg)
{
	if (!this.entities.length)
		return;

	const cmpTimer = Engine.QueryInterface(SYSTEM_ENTITY, IID_Timer);
	const now = cmpTimer.GetTime();
	if (now < this.nextAttackedMessageTime)
		return;
	this.nextAttackedMessageTime = now + this.NotificationInterval;

	for (const ent of this.entities)
		Engine.PostMessage(ent, MT_GuardedAttacked, { "guarded": this.entity, "data": msg });
};

/**
 * If an entity is captured, or about to be killed (so its owner
 * changes to '-1') or if diplomacy changed, update the guards list
 */
Guard.prototype.OnOwnershipChanged = function(msg)
{
	if (!this.entities.length)
		return;
	this.CheckGuards(msg.to == INVALID_PLAYER);
};

Guard.prototype.OnDiplomacyChanged = function(msg)
{
	if (!this.entities.length)
		return;
	this.CheckGuards();
};

Guard.prototype.CheckGuards = function(force = false)
{
	const entities = this.GetEntities();
	for (const ent of entities)
	{
		if (force || !IsOwnedByMutualAllyOfEntity(this.entity, ent))
		{
			const cmpUnitAI = Engine.QueryInterface(ent, IID_UnitAI);
			if (cmpUnitAI && cmpUnitAI.IsGuardOf() && cmpUnitAI.IsGuardOf() == this.entity)
				cmpUnitAI.RemoveGuard();
			else
				this.RemoveGuard(ent);
		}
	}
};

Engine.RegisterComponentType(IID_Guard, "Guard", Guard);
