Trigger.prototype.DisplayIntroduction = function()
{
	TriggerHelper.DisplayNarrativePage({
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.LARGE,
		"title": markForTranslation("The Peloponnesian War"),
		"image": "peloponnesian_war/greece_map.png",
		"titleSize": "555 35 100% 65",
		"text": markForTranslation("In the 431st year BC, Greece is at war. On one side stands the Delian League, an alliance-empire hybrid comprised of the democracy of Athens and many island states and colonies. On the other stands the Peloponnesian League, an alliance led by the kingdom of Sparta. Both leagues were founded to either defend or continue the attack on their mutual enemy, the Persians, but now their weapons would be turned onto their own kind in fierce war that would extend almost across the entire Greek world."),
		"textSize": "555 65 100% 100%"
	});
};


Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger).RegisterTrigger("OnInitGame", "DisplayIntroduction", { "enabled": true });
