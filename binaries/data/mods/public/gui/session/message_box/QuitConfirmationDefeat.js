/**
 * This class will spawn a dialog asking to exit the game in case the user was an assigned player who has been defeated.
 */
class QuitConfirmationDefeat extends QuitConfirmation
{
	constructor(closeSession)
	{
		super();

		if (Engine.IsAtlasRunning())
			return;

		this.confirmHandler = undefined;
		registerPlayersFinishedHandler(this.onPlayersFinished.bind(this, closeSession));
	}

	onPlayersFinished(closeSession, players, won)
	{
		if (players.indexOf(Engine.GetPlayerID()) == -1)
			return;

		// Defer simulation result until
		// 1. the loading screen finished for all networked clients (g_IsNetworkedActive)
		// 2. all messages modifying g_Players victory state were processed (next turn)
		this.confirmHandler = this.confirmExit.bind(this, won, closeSession);
		registerSimulationUpdateHandler(this.confirmHandler);
	}

	confirmExit(won, closeSession)
	{
		if (g_IsNetworked && !g_IsNetworkedActive)
			return;

		unregisterSimulationUpdateHandler(this.confirmHandler);

		// Don't invite the host to exit if other humans are still playing.
		const askExit = !Engine.HasNetServer() || g_Players.every((player, i) =>
			i == 0 ||
			player.state != "active" ||
			g_InitAttributes.settings.PlayerData[i].AI != "");

		this.Title = won ? this.TitleVictory : this.TitleDefeated;

		this.Caption =
			g_PlayerStateMessages[won ? "won" : "defeated"] +
			(askExit ? "\n" + this.Question : "");

		this.Buttons = askExit ? super.Buttons : undefined;

		this.display(closeSession);
	}
}

QuitConfirmationDefeat.prototype.TitleVictory = translate("VICTORIOUS!");
QuitConfirmationDefeat.prototype.TitleDefeated = translate("DEFEATED!");

QuitConfirmationDefeat.prototype.Question = translate("Do you want to quit?");
