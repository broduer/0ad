GameSettings.prototype.Attributes.PlayerUnassigned = class PlayerUnassigned
{
	onFinalizeAttributes(attribs, playerAssignments)
	{
		for (const player of attribs.settings.PlayerData)
			player.Unassigned = !player.Removed && !player.AI;

		for (const assignment of Object.values(playerAssignments))
			if (assignment.player >= 0)
				attribs.settings.PlayerData[assignment.player - 1].Unassigned = false;
	}
};
