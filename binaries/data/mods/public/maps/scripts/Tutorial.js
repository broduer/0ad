// Needs to be kept in sync with the one in gui/session/tutorial/Tutorial.js
var TUTORIAL_STEP_TYPE = deepfreeze({
	"INSTRUCTION": 1
});

Engine.RegisterGlobal("TUTORIAL_STEP_TYPE", TUTORIAL_STEP_TYPE);

Trigger.prototype.InitTutorial = function(data)
{
	this.stepIndex = 0;
	this.tutorialEvents = [];

	// Register needed triggers
	this.RegisterTrigger("OnDeserialized", "DeserializedAction", { "enabled": true });
	this.RegisterTrigger("OnPlayerCommand", "BasePlayerCommandAction", { "enabled": true });

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
			const action = key.substring(2) + "Action";
			this.RegisterTrigger(key, action, { "enabled": false });
			this.tutorialEvents.push(key);
		}
	}

	this.NextStep();
};

Trigger.prototype.NextStep = function(deserializing = false)
{
	if (this.stepIndex > this.tutorialSteps.length)
		return;
	const step = this.tutorialSteps[this.stepIndex];

	Trigger.prototype.Init = step.Init || null;
	if (!deserializing && this.Init)
		this.Init();

	for (const event of this.tutorialEvents)
	{
		const action = event.substring(2) + "Action";
		if (step[event])
		{
			Trigger.prototype[action] =
				event == "OnPlayerCommand" ?
					(msg) =>
					{
						// Don't forward tutorial continue commands, since the step trigger actions aren't supposed to handle them,
						// plus we might have already loaded in the next step.
						if (msg.cmd.type != "dialog-answer" || msg.cmd.tutorial != "continue")
							step.OnPlayerCommand.call(this, msg);
					} :
					step[event];

			this.EnableTrigger(event, action);
		}
		else
			this.DisableTrigger(event, action);
	}

	Trigger.prototype.IsDone = step.IsDone || (() => false);
	const showContinueButton = this.IsDone() || (step.panelData.showContinueButton === undefined ?
		this.tutorialEvents.every(event => !step[event]) :
		step.panelData.showContinueButton
	);

	this.DisplayStep(step, showContinueButton, ++this.stepIndex == this.tutorialSteps.length);
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

Trigger.prototype.BasePlayerCommandAction = function(msg)
{
	if (msg.cmd.type == "dialog-answer" && msg.cmd.tutorial == "continue")
		this.NextStep();
};

Trigger.prototype.DeserializedAction = function()
{
	this.stepIndex = Math.max(0, this.stepIndex - 1);

	// Display messages from already processed steps
	for (let i = 0; i < this.stepIndex; ++i)
		this.DisplayStep(this.tutorialSteps[i], false, false);

	this.NextStep(true);
};
