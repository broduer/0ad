// Needs to be kept in sync with the one in gui/session/tutorial/Tutorial.js
var TUTORIAL_STEP_TYPE = deepfreeze({
	"INSTRUCTION": 1
});

Engine.RegisterGlobal("TUTORIAL_STEP_TYPE", TUTORIAL_STEP_TYPE);

Trigger.prototype.InitTutorial = function(data)
{
	this.index = 0;
	this.fullText = "";
	this.tutorialEvents = [];

	// Register needed triggers
	this.RegisterTrigger("OnDeserialized", "OnDeserializedTrigger", { "enabled": true });
	this.RegisterTrigger("OnPlayerCommand", "OnPlayerCommandTrigger", { "enabled": false });
	this.tutorialEvents.push("OnPlayerCommand");

	for (const step of this.tutorialSteps)
	{
		for (const key in step)
		{
			if (typeof step[key] !== "function" || this.tutorialEvents.indexOf(key) != -1)
				continue;
			if (key == "Init")
				continue;
			if (key == "IsDone")
				continue;
			const action = key + "Trigger";
			this.RegisterTrigger(key, action, { "enabled": false });
			this.tutorialEvents.push(key);
		}
	}

	this.NextStep();
};

Trigger.prototype.NextStep = function(deserializing = false)
{
	if (this.index > this.tutorialSteps.length)
		return;
	const step = this.tutorialSteps[this.index];
	let needDelay = true;
	let readyButton = false;

	Trigger.prototype.Init = step.Init || null;
	if (!deserializing && this.Init)
		this.Init();

	Trigger.prototype.IsDone = step.IsDone || (() => false);
	const stepAlreadyDone = this.IsDone();

	for (const event of this.tutorialEvents)
	{
		const action = event + "Trigger";
		if (step[event])
		{
			Trigger.prototype[action] = step[event];
			this.EnableTrigger(event, action);
			if (!stepAlreadyDone)
				needDelay = false;
		}
		else
			this.DisableTrigger(event, action);
	}

	// Steps without actions to be performed by the player must have
	// - either the property delay (a value > 0 to wait for a given time, and -1 to display the Ready button)
	// - or no trigger functions (needDelay will be set automatically to true and the Ready button displayed)
	if (step.delay || needDelay)
	{
		if (step.delay && step.delay > 0)
			this.DoAfterDelay(+step.delay, "NextStep", {});
		else
		{
			this.EnableTrigger("OnPlayerCommand", "OnPlayerCommandTrigger");
			Trigger.prototype.OnPlayerCommandTrigger = function(msg)
			{
				if (msg.cmd.type == "dialog-answer" && msg.cmd.tutorial && msg.cmd.tutorial == "ready")
					this.NextStep();
			};
			readyButton = true;
		}
	}

	this.DisplayStep(step, readyButton, ++this.index == this.tutorialSteps.length);
};

Trigger.prototype.DisplayStep = function(step, readyButton = false, leave = false)
{
	const cmpGUIInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
	cmpGUIInterface.PushNotification({
		"type": "tutorial",
		"players": [1],
		"step": {
			"type": step.type,
			"panelData": {
				...step.panelData,
				"readyButton": readyButton,
				"leave": leave
			}
		}
	});
};

Trigger.prototype.DisplayWarning = function(warning)
{
	const cmpGUIInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
	cmpGUIInterface.PushNotification({
		"type": "tutorial",
		"players": [1],
		"warning": warning
	});
};

Trigger.prototype.OnDeserializedTrigger = function()
{
	this.index = Math.max(0, this.index - 1);

	// Display messages from already processed steps
	for (let i = 0; i < this.index; ++i)
		this.DisplayStep(this.tutorialSteps[i], false, false);

	this.NextStep(true);
};
