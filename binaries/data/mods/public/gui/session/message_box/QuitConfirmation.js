class QuitConfirmation extends SessionMessageBox
{
}

QuitConfirmation.prototype.Title =
	translate("Confirmation");

QuitConfirmation.prototype.Caption =
	translate("The game has finished, what do you want to do?");

QuitConfirmation.prototype.Buttons =
	[
		{
		// Translation: Shown in the Dialog that shows up when the game finishes
			"caption": translate("Stay"),
			"onPress": resumeGame
		},
		{
		// Translation: Shown in the Dialog that shows up when the game finishes
			"caption": translate("Quit and View Summary"),
			"onPress": function() { this.closeSession(true); }
		},
		{
		// Translation: Shown in the Dialog that shows up when the game finishes
			"caption": translate("Quit"),
			"onPress": function() { this.closeSession(false); }
		}
	];

QuitConfirmation.prototype.Width = 600;
QuitConfirmation.prototype.Height = 200;

QuitConfirmation.prototype.ResumeOnClose = false;

