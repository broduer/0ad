class OutOfSyncNetwork extends SessionMessageBox
{
	constructor()
	{
		super();
		registerNetworkOutOfSyncHandler(this.onNetworkOutOfSync.bind(this));
	}

	/**
	 * The message object is constructed in CNetClientTurnManager::OnSyncError.
	 */
	onNetworkOutOfSync(msg)
	{
		const txt = [
			sprintf(translate("Out-Of-Sync error on turn %(turn)s."), {
				"turn": msg.turn
			}),

			sprintf(translateWithContext("Out-Of-Sync", "Players: %(players)s"), {
				"players": msg.players.join(translateWithContext("Separator for a list of players", ", "))
			}),

			msg.hash == msg.expectedHash ?
				translateWithContext("Out-Of-Sync", "Your game state is identical to the hosts game state.") :
				translateWithContext("Out-Of-Sync", "Your game state differs from the hosts game state."),

			"",
			translateWithContext("Out-Of-Sync", "Ensure all players use the same mods."),
			translateWithContext("Out-Of-Sync", 'Click on "Report a Bug" in the main menu to help fix this.'),
			sprintf(translateWithContext("Out-Of-Sync", "Replay saved to %(filepath)s"), {
				"filepath": escapeText(msg.path_replay)
			}),
			msg.dumpPending ?
				sprintf(translateWithContext("Out-Of-Sync", "Diagnostic dump for turn %(turn)s (if reached): %(filepath)s"), {
					"turn": msg.dumpTurn,
					"filepath": escapeText(msg.path_oos_dump)
				}) :
				translateWithContext("Out-Of-Sync", "No diagnostic dump was scheduled.")
		];

		this.Caption = txt.join("\n");
		this.display();
	}
}

OutOfSyncNetwork.prototype.Width = 600;
OutOfSyncNetwork.prototype.Height = 280;

OutOfSyncNetwork.prototype.Title = translate("Out of Sync");
