// Needs to be kept in sync with the one in gui/session/tutorial/Tutorial.js
var TUTORIAL_STEP_TYPE = deepfreeze({
	"INSTRUCTION": 1
});

Engine.RegisterGlobal("TUTORIAL_STEP_TYPE", TUTORIAL_STEP_TYPE);

Trigger.prototype.InitTutorial = function(data)
{
	this.index = 0;
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

	Trigger.prototype.Init = step.Init || null;
	if (!deserializing && this.Init)
		this.Init();

	for (const event of this.tutorialEvents)
	{
		const action = event + "Trigger";
		if (step[event])
		{
			Trigger.prototype[action] = step[event];
			this.EnableTrigger(event, action);
		}
		else
			this.DisableTrigger(event, action);
	}

	Trigger.prototype.IsDone = step.IsDone || (() => false);
	const showContinueButton =
		step.panelData.showContinueButton === undefined ?
			this.IsDone() || this.tutorialEvents.every(event => !step[event]) :
			step.panelData.showContinueButton;

	if (showContinueButton)
	{
		this.EnableTrigger("OnPlayerCommand", "OnPlayerCommandTrigger");
		Trigger.prototype.OnPlayerCommandTrigger = function(msg)
		{
			if (msg.cmd.type == "dialog-answer" && msg.cmd.tutorial && msg.cmd.tutorial == "continue")
				this.NextStep();
		};
	}

	this.DisplayStep(step, showContinueButton, ++this.index == this.tutorialSteps.length);
};

Trigger.prototype.DisplayStep = function(step, showContinueButton = false, isLast = false)
{
	const cmpGUIInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_GuiInterface);
	cmpGUIInterface.PushNotification({
		"type": "tutorial",
		"players": [1],
		"step": {
			"type": step.type,
			"panelData": {
				...step.panelData,
				"showContinueButton": showContinueButton,
				"isLast": isLast
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
