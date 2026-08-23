// The characters' entity IDs.
const CHARACTER = {
	"IPHICRATES": 9339,
	"THEMISTOCLES": 9340,
	"PERICLES": 9341
};

Trigger.prototype.PlayOpeningCutscene = function()
{
	const cmpCinemaInterface = Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaInterface);
	if (!cmpCinemaInterface)
		return;

	cmpCinemaInterface.PlayExistingPath(this.FirstCinemaPathName);
	cmpCinemaInterface.PlayCustomPath(this.SecondCinemaPathData, {
		// Since the next actions here are fixed, passing it to the callback would be pointless in practice.
		// This here is just supposed to demonstrate how to pass any arguments to it in general.
		"action": "ContinueAfterOpeningCutscene", "args": [
			{ "action": "ShowPericlesSpeech", "delay": 5000 },
			{ "action": "ShowThemistoclesIphicratesDialogue", "delay": 17000 }
		]
	});
	cmpCinemaInterface.ShowSpeechSequence(this.OpeningSpeechSequence);
};

Trigger.prototype.ContinueAfterOpeningCutscene = function(sequences)
{
	for (const sequence of sequences)
		this.DoAfterDelay(sequence.delay, sequence.action);
};

Trigger.prototype.ShowPericlesSpeech = function()
{
	Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaInterface)?.ShowSpeechSequence(
		[this.PericlesSpeech], { "moveCameraToSpeakingUnit": false, "enableCutsceneMode": false }
	);
};

Trigger.prototype.ShowThemistoclesIphicratesDialogue = function()
{
	Engine.QueryInterface(SYSTEM_ENTITY, IID_CinemaInterface)?.ShowSpeechSequence(
		this.ThemistoclesIphicratesDialogue, { "moveCameraToSpeakingUnit": true, "enableCutsceneMode": true }
	);
};

Trigger.prototype.FirstCinemaPathName = "path1";
Trigger.prototype.SecondCinemaPathData = {
	"name": "path2",
	"timescale": 1,
	"orientation": "target",
	"mode": "ease_inout",
	"style": "default",
	"positionNodes": [
		{
			"position": new Vector3D(290.31104, 49.29522, 400.7906),
			"deltaTime": 0
		},
		{
			"position": new Vector3D(254.64576, 66.57055, 352.07383),
			"deltaTime": 4
		},
		{
			"position": new Vector3D(188.43287, 85.68216, 318.43021),
			"deltaTime": 4
		},
		{
			"position": new Vector3D(187.1648, 84.94903, 315.23832),
			"deltaTime": 4
		}
	],
	"targetNodes": [
		{
			"position": new Vector3D(264.95313, 46.68482, 365.76944),
			"deltaTime": 0
		},
		{
			"position": new Vector3D(238.60918, 56.95272, 311.4221),
			"deltaTime": 4
		},
		{
			"position": new Vector3D(234.26212, 58.81025, 301.79963),
			"deltaTime": 4
		},
		{
			"position": new Vector3D(235.16155, 58.12488, 299.04053),
			"deltaTime": 4
		}
	]
};

Trigger.prototype.OpeningSpeechSequence = [
	{
		"duringPaths": ["path1"],
		// TODO: Translate these?
		"lines": ["The story of ancient Athens is both fascinating and inspiring. Today praised as the birthplace of democracy, it was a place built and run by its people. But that wasn't to say it lacked outstanding individuals; people who found a way to use their remarkable brilliance in an inspiring and memorable way, by devoting their lives to make their homeland a better place for fellow citizens, defend it from foreign threats, and lead them all into a better future."],
	},
	{
		"duringPaths": ["path2"],
		"lines": ["Admittedly... that might all have been a bit of an exaggeration. But you get the idea.\nHere are three of the most famous people of Classical Athens: Pericles, Iphicrates, and Themistocles."]
	}
];

Trigger.prototype.PericlesSpeech = {
	"speakingUnit": CHARACTER.PERICLES,
	"lines": [
		"Greetings, fellow citizens. I'm glad to finally meet you two. Thanks for joining this gathering and making your way to this totally random hilltop.",
	],
	"lineDurations": [8000]
};

Trigger.prototype.ThemistoclesIphicratesDialogue = [
	{
		"speakingUnit": CHARACTER.PERICLES,
		"lines": [
			"Let's start by introducing ourselves. I'll go first: My name is Pericles and I am an Athenian statesman from the later 5th century BC."
		]
	},
	{
		"speakingUnit": CHARACTER.THEMISTOCLES,
		"lines": ["Hi, my name is Themistocles and I am an Athenian general and politician from the 5th century BC."]
	},
	{
		"speakingUnit": CHARACTER.IPHICRATES,
		"lines": [
			"Hi, I'm Iphicrates, an Athenian general too, but from the 4th century BC.",
			"I have the reputation of being a gifted commander with comprehensive knowledge military tactics and trends, unmatched by other commanders at the time.\nAmong my many heroic triumphs were victories over Sparta in the Corinthian War. However, I am even more famous for the feat of reforming the Athenian military by developing a new type of light infantry called the \u201Cpeltasts\u201D.",
			"As the son of a shoemaker, I even designed a new type of boots for my soldiers called 'Iphicratides'. They were not only more lightweight, but also easier to untie than..."
		]
	},
	{
		"speakingUnit": CHARACTER.THEMISTOCLES,
		"lines": ["Stop bragging. If I hadn't kicked out the Persians at Salamis in 480 BC, you wouldn't even have been born."]
	},
	{
		"speakingUnit": CHARACTER.IPHICRATES,
		"lines": ["At least I wasn't -- unlike you -- ostracised by the people of Athens later and then fled to our enemy Persia."]
	},
	{
		"speakingUnit": CHARACTER.PERICLES,
		"lines": [
			"\\[Sigh] I'm not going to argue with you childish quarrelers.",
			"All I'm gonna say is, there is more to being a good leader than just fighting battles or winning wars."
		]
	},
	{
		"speakingUnit": CHARACTER.IPHICRATES,
		"lines": ["It doesn't suprise me that you're the one to say that, considering you never managed to make it big as a general."]
	},
	{
		"speakingUnit": CHARACTER.PERICLES,
		"lines": [
			"Before criticising my leadership skills, can you first explain to me how you ended up partaking a military campaign [font=\"sans-italic-18\"]against[/font] Athens? Be grateful I don't straight up call you a traitor.",
			"You should both be more appreciative of my achievements. It's for a reason that the time in which I led Athens is called its \u201DGolden Age\u201D. Neither of you two can say that about yourselves, can you?"
		]
	},
	{
		"speakingUnit": CHARACTER.THEMISTOCLES,
		"lines": [
			"Appreciative of what? Appreciative of your amazing idea in the Peloponnesian War of bunkering yourself and cramming our entire population into the city walls, where inevitably an awful plague broke out that ended up wiping out a quarter of the entire population?",
			"You know, I might not have a \u201CGolden Age\u201D attributed to my name, but at least I have a sense of hygiene."
		]
	}
];

Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger).RegisterTrigger("OnInitGame", "PlayOpeningCutscene", { "enabled": true });
