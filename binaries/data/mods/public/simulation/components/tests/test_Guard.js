Engine.LoadComponentScript("interfaces/Guard.js");
Engine.LoadComponentScript("interfaces/Timer.js");
Engine.LoadComponentScript("Guard.js");

const guarded = 10;
const guards = [20, 21, 22];

let currentTime = 0;
AddMock(SYSTEM_ENTITY, IID_Timer, {
	"GetTime": () => currentTime
});

let notified = [];
Engine.PostMessage = function(ent, iid, message)
{
	if (iid == MT_GuardedAttacked)
		notified.push(ent);
};

const cmpGuard = ConstructComponent(guarded, "Guard", null);
for (const guard of guards)
	cmpGuard.AddGuard(guard);
TS_ASSERT_UNEVAL_EQUALS(cmpGuard.GetEntities(), guards);

const attackMsg = { "attacker": 30, "target": guarded, "type": "Ranged", "damage": 5 };

// The first attack notifies all guards.
cmpGuard.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, guards);

// Attacks shortly after don't, to avoid spamming the guards (#6007).
notified = [];
cmpGuard.OnAttacked(attackMsg);
currentTime += cmpGuard.NotificationInterval / 2;
cmpGuard.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, []);

// Once the notification interval has passed, guards are notified again.
currentTime += cmpGuard.NotificationInterval / 2;
cmpGuard.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, guards);

// A new guard is notified without delay.
notified = [];
cmpGuard.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, []);
const newGuard = 23;
cmpGuard.AddGuard(newGuard);
cmpGuard.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, [...guards, newGuard]);

// The throttling survives a serialization cycle.
notified = [];
const cmpGuard2 = SerializationCycle(cmpGuard);
cmpGuard2.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, []);
currentTime += cmpGuard2.NotificationInterval;
cmpGuard2.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, [...guards, newGuard]);

// Guards removed from the list are not notified anymore.
notified = [];
cmpGuard2.RemoveGuard(newGuard);
currentTime += cmpGuard2.NotificationInterval;
cmpGuard2.OnAttacked(attackMsg);
TS_ASSERT_UNEVAL_EQUALS(notified, guards);
