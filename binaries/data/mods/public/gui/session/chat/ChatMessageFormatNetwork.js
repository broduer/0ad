/**
 * This class parses network events sent from the NetClient, such as players connecting or disconnecting from the game.
 */
class ChatMessageFormatNetwork
{
}

ChatMessageFormatNetwork.clientlist = class
{
	parse()
	{
		return { "text": getUsernameList() };
	}
};

ChatMessageFormatNetwork.connect = class
{
	parse(msg)
	{
		return {
			"text": sprintf(
				g_PlayerAssignments[msg.guid].player != -1 ?
					// Translation: A player that left the game joins again
					translate("%(player)s is starting to rejoin the game.") :
					// Translation: A player joins the game for the first time
					translate("%(player)s is starting to join the game."),
				{ "player": colorizePlayernameByGUID(msg.guid) })
		};
	}
};

ChatMessageFormatNetwork.disconnect = class
{
	parse(msg)
	{
		return {
			"text": sprintf(translate("%(player)s has left the game."), {
				"player": colorizePlayernameByGUID(msg.guid)
			})
		};
	}
};

ChatMessageFormatNetwork.moderation = class
{
	parse(msg)
	{
		return {
			"text": sprintf(
				formatModerationResponse(msg.code),
				{
					"targetPlayer": colorizePlayernameHelper(
						msg.targetPlayer,
						g_Players.findIndex(p => p.name == msg.targetPlayer)
					),
					"originPlayer": colorizePlayernameHelper(
						msg.originPlayer,
						g_Players.findIndex(p => p.name == msg.originPlayer)
					)
				})
		};
	}
};

ChatMessageFormatNetwork.moderationlisting = class
{
	parse(msg)
	{
		return { "text": formatModerationListing(msg.json, true) };
	}
};

ChatMessageFormatNetwork.rejoined = class
{
	parse(msg)
	{
		return {
			"text": sprintf(
				g_PlayerAssignments[msg.guid].player != -1 ?
					// Translation: A player that left the game joins again
					translate("%(player)s has rejoined the game.") :
					// Translation: A player joins the game for the first time
					translate("%(player)s has joined the game."),
				{ "player": colorizePlayernameByGUID(msg.guid) })
		};
	}
};
