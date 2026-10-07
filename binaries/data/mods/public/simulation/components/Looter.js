function Looter() {}

Looter.prototype.Schema =
	"<empty/>";

Looter.prototype.Serialize = null; // We have no dynamic state to save

/**
 * Try to collect loot from target entity
 */
Looter.prototype.Collect = function(targetEntity)
{
	var cmpLoot = Engine.QueryInterface(targetEntity, IID_Loot);
	if (!cmpLoot)
		return;

	// Collect resources carried by workers and traders
	var cmpResourceGatherer = Engine.QueryInterface(targetEntity, IID_ResourceGatherer);
	var cmpTrader = Engine.QueryInterface(targetEntity, IID_Trader);

	const resourcesCarried = calculateCarriedResources(
		cmpResourceGatherer && cmpResourceGatherer.GetCarryingStatus(),
		cmpTrader && cmpTrader.GetGoods()
	);

	// Loot resources as defined in the templates
	const lootTemplate = cmpLoot.GetResources();
	const resources = {};
	for (const type of Resources.GetCodes())
		resources[type] =
			ApplyValueModificationsToEntity(
				"Looter/Resource/"+type, lootTemplate[type] || 0, this.entity) +
			(resourcesCarried[type] || 0);

	// Transfer resources
	var cmpPlayer = QueryOwnerInterface(this.entity);
	if (cmpPlayer)
		cmpPlayer.AddResources(resources);

	// Update statistics
	var cmpStatisticsTracker = QueryOwnerInterface(this.entity, IID_StatisticsTracker);
	if (cmpStatisticsTracker)
		cmpStatisticsTracker.IncreaseLootCollectedCounter(resources);

	if (cmpPlayer && cmpTrader)
		this.NotifyTradeGoodsIntercepted(targetEntity, cmpPlayer.GetPlayerID(), cmpTrader.GetGoods());
};

/**
 * Notify that the goods carried by a trader were intercepted.
 */
Looter.prototype.NotifyTradeGoodsIntercepted = function(targetEntity, looterOwner, goods)
{
	const amounts = calculateCarriedResources(undefined, goods);
	if (!goods?.type || !amounts[goods.type])
		return;

	const cmpVictimOwnership = Engine.QueryInterface(targetEntity, IID_Ownership);
	if (!cmpVictimOwnership)
		return;

	Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface)?.PushNotification({
		"type": "trade-intercepted",
		"players": [looterOwner],
		"victim": cmpVictimOwnership.GetOwner(),
		"amounts": amounts
	});
};

Engine.RegisterComponentType(IID_Looter, "Looter", Looter);
