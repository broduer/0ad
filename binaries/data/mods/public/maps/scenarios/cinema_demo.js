Trigger.prototype.CounterMessage = function(data)
{
	Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface)?.PushNotification({
		"players": [1, 2],
		"message": markForTranslation("Cutscene starts after 5 seconds"),
		"translateMessage": true
	});
};

Trigger.prototype.StartCutscene = function()
{
	const cmpCinemaManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaManager);
	if (!cmpCinemaManager)
		return;
	cmpCinemaManager.PushPathToQueue("test");
	cmpCinemaManager.StartPlayingQueue();
};

const cmpTrigger = Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger);
cmpTrigger.DoAfterDelay(1000, "CounterMessage", {});
cmpTrigger.DoAfterDelay(5000, "StartCutscene", {});
