/**
 * Prints out the documentation for moderation commands. Requires addChatMessage.
 */
function moderationHelp()
{
	if (!g_IsController)
	{
		addChatMessage({
			"type": "system",
			"text": translate("Moderation commands are only available for the game host and for designated moderation helpers.")
		});
	}

	addChatMessage({
		"type": "system",
		"text": sprintf(translate("Moderators can mute or unmute a player using %(muteCommand)s and %(unmuteCommand)s. Example: %(usageExample)s"),
		{
			"muteCommand": "/mute",
			"unmuteCommand": "/unmute",
			"usageExample": "/mute annoying_player",
		})
	});

	addChatMessage({
		"type": "system",
		"text": sprintf(translate("Moderators can kick a player out of the game using %(kickCommand)s. The user is allowed to rejoin the game. Example: %(usageExample)s"),
		{
			"kickCommand": "/kick",
			"usageExample": "/kick annoying_player",
		})
	});

	addChatMessage({
		"type": "system",
		"text": sprintf(translate("Moderators can ban or unban a player using %(banCommand)s and %(unbanCommand)s. Example: %(usageExample)s"),
		{
			"banCommand": "/ban",
			"unbanCommand": "/unban",
			"usageExample": "/ban annoying_player",
		})
	});

	addChatMessage({
		"type": "system",
		"text": sprintf(translate("Moderators can mute, kick, or ban all spectators at once, using %(muteCommand)s, %(kickCommand)s, or %(banCommand)s."),
		{
			"muteCommand": "/mutespecs",
			"kickCommand": "/kickspecs",
			"banCommand": "/banspecs",
		})
	});

	addChatMessage({
		"type": "system",
		"text": sprintf(translate("Moderators can review the current moderation status of users using the %(listingCommand)s command."),
		{
			"listingCommand": "/listmoderation",
		})
	});

	if (g_IsController)
	{
		addChatMessage({
			"type": "system",
			"text": sprintf(translate("The host can grant or remove moderation privileges to helpers using %(helperCommand)s and %(unhelperCommand)s. Example: %(usageExample)s"),
			{
				"helperCommand": "/helper",
				"unhelperCommand": "/unhelper",
				"usageExample": "/helper trusted_player",
			})
		});

		addChatMessage({
			"type": "system",
			"text": sprintf(translate("The host can entirely revoke all mutes, all bans, and all helper statuses using %(clearCommand)s. This may be useful if a helper has abused their privileges."),
			{
				"clearCommand": "/clearmoderation",
			})
		});
	}

}

/**
 * Must be kept in sync with source/network/NetModeration.h.
 * This function returns a string which may contain placeholders named %(targetPlayer) and %(originPlayer).
 * The caller of this function should run sprintf on the returned string.
 */
function formatModerationResponse(code)
{
	switch (code)
	{
	case 1: return translate("You are not allowed to perform this moderation action.");
	case 2: return translate("Player %(targetPlayer)s was not found and was not impacted by this moderation action.");
	case 3: return translate("You are muted and cannot send chat messages.");
	case 4: return translate("%(targetPlayer)s was muted by %(originPlayer)s.");
	case 5: return translate("%(targetPlayer)s was unmuted by %(originPlayer)s.");
	case 6: return translate("%(targetPlayer)s was not found in the muted users list.");
	case 7: return translate("%(targetPlayer)s was kicked from the game by %(originPlayer)s.");
	case 8: return translate("%(targetPlayer)s was banned from the game by %(originPlayer)s.");
	case 9: return translate("%(targetPlayer)s was unbanned by %(originPlayer)s.");
	case 10: return translate("%(targetPlayer)s was not found in the banned users list.");
	case 11: return translate("%(targetPlayer)s was designated as a moderation helper by the host.");
	case 12: return translate("%(targetPlayer)s was not found and was not designated as a moderation helper.");
	case 13: return translate("%(targetPlayer)s was removed from moderation helpers by the host.");
	case 14: return translate("%(targetPlayer) was not found in the moderation helpers list.");
	case 15: return translate("The host has unbanned and unmuted everyone, and has removed all moderation helpers.");
	default:
		warn("Invalid moderation response from the host: code " + code);
		return sprintf(translate("\\[Invalid moderation code %(code)s]"), { "code": code });
	}
}

/**
 * This function returns a formatted message listing muted users, banned users, and moderation helpers.
 * If @param colorize is set to true, it will colorize player names and GUIDs (for instance in the game session),
 * else it will not (for instance in the gamesetup chat).
 */
function formatModerationListing(json, colorize)
{
	let nameFormatter = colorize ?
		(name) => colorizePlayernameHelper(name, g_Players.findIndex(p => p.name == name)) :
		(name) => name;

	let guidFormatter = colorize ?
		(guid) => colorizePlayernameByGUID(guid) :
		(guid) => guid;

	let ret = translate("Moderation lists :") + "\n";

	let listing = JSON.parse(json);

	if (listing["muted-players"].length > 0)
		ret += translate("Muted players: ") +
			listing["muted-players"].map(nameFormatter).join(", ") +
			"\n";
	else
		ret += translate("There are no muted players.") + "\n";

	if ("muted-ips" in listing)
	{
		if (listing["muted-ips"].length > 0)
			ret += translate("Muted IP addresses: ") +
				listing["muted-ips"].join(", ") +
				"\n";
		else
			ret += translate("There are no muted IP addresses.") + "\n";
	}

	if (listing["banned-players"].length > 0)
		ret += translate("Banned players: ") +
			listing["banned-players"].map(nameFormatter).join(", ") +
			"\n";
	else
		ret += translate("There are no banned players.") + "\n";

	if ("banned-ips" in listing)
	{
		if (listing["banned-ips"].length > 0)
			ret += translate("Banned IP addresses: ") +
				listing["banned-ips"].join(", ") +
				"\n";
		else
			ret += translate("There are no banned IP addresses.") + "\n";
	}

	if (listing["moderation-helper-guids"].length > 0)
		ret += translate("Moderation helpers: ") +
			listing["moderation-helper-guids"].map(guidFormatter).join(", ") +
			"\n";
	else
		ret += translate("There are no designated moderation helpers.") + "\n";

	return ret;
}

function setModerationHelper(user, toggle)
{
	if (g_IsController)
		Engine.SetModerationHelper(user, toggle);
	else
		addChatMessage({
		"type": "system",
		"text": translate("Only the host can manage moderation helper permissions.")
	});
}

function moderateObservers(action)
{
	for (let assignment of g_PlayerAssignments)
		if (assignment.player == -1)
			action(assignment.name);
}


/**
 * Execute a moderation command. Requires addChatMessage.
 *
 * @param {string} input
 * @returns {boolean} whether a command was executed
 */
function executeModerationCommand(input)
{
	if (input.indexOf("/") != 0)
		return false;

	let command = input.split(" ", 1)[0];
	let argument = input.substr(command.length + 1).trim();

	let noArg = argument.length == 0;

	switch(command)
	{
	case "/mute": if (noArg) return false; Engine.MutePlayer(argument, true); return true;
	case "/unmute": if (noArg) return false; Engine.MutePlayer(argument, false); return true;
	case "/kick": if (noArg) return false; Engine.KickPlayer(argument); return true;
	case "/ban": if (noArg) return false; Engine.BanPlayer(argument, true); return true;
	case "/unban": if (noArg) return false; Engine.BanPlayer(argument, false); return true;
	case "/helper": if (noArg) return false; setModerationHelper(argument, true); return true;
	case "/unhelper": if (noArg) return false; setModerationHelper(argument, false); return true;
	case "/mutespecs": moderateObservers((user) => { Engine.MutePlayer(user, true); }); return true;
	case "/kickspecs": moderateObservers((user) => { Engine.KickPlayer(user); }); return true;
	case "/banspecs": moderateObservers((user) => { Engine.BanPlayer(user, true); }); return true;
	case "/clearmoderation": Engine.ClearModerationList(); return true;
	case "/listmoderation": Engine.QueryModerationList(); return true;
	case "/help": moderationHelp(); return true;
	default:
		return false;
	}
}
