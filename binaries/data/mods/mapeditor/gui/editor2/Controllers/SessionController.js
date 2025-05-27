import { Observable } from "gui/global/Observable.js";
import { Phases, Provenances } from "gui/editor2/Constants/SessionController.js";
import { ControllerNames } from "gui/editor2/Constants/Controllers.js";

export class SessionController extends Observable
{
	constructor(setupWindow)
	{
		super();
		this.setupWindow = setupWindow;
		this.phase = Phases.NO_MAP;
		this.provenance = Provenances.EMPTY;
		this.attribs = null;
		this.cleanAtDepth = 0;

		globalThis.reallyStartGame = this.onGameReady.bind(this);
	}

	get isDirty()
	{
		return this.phase === Phases.READY &&
            this.setupWindow.controllers[ControllerNames.COMMANDS_CONTROLLER].depth() !== this.cleanAtDepth;
	}

	async transition(attribs, provenance)
	{
		if (this.phase === Phases.LOADING)
			return;
		if (this.isDirty && !await this.confirmDiscard())
			return;

		if (!attribs)
			attribs = {};

		attribs.editorType = "1"; // TODO: Use MapEditor for a editor game
		attribs.gameSpeed = 0;
		this.pending = { attribs, provenance };
		this.phase = Phases.LOADING;
		this.setupWindow.onGameUnload();

		if (MapEditor.IsEditorRunning())
			Engine.EndGame(); // TODO: Use MapEditor for end a editor game

		Engine.StartGame(attribs, 1, false); // TODO: Use MapEditor for start a editor game
	}

	async confirmDiscard()
	{
		const buttonIndex = await messageBox(
			400, 200,
			this.ConfirmMessage,
			this.ConfirmTitle,
			[this.ConfirmButtonNo, this.ConfirmButtonYes]);
		return buttonIndex === 1;
	}

	onGameReady()
	{
		this.attribs = this.pending.attribs;
		this.provenance = this.pending.provenance;
		this.pending = null;
		this.phase = Phases.READY;

		MapEditor.RevealMap();
		Engine.GameView_SetConstrainCameraEnabled(false);

		this.setupWindow.onGameLoad(this.attribs);
		this.setupWindow.controllers[ControllerNames.COMMANDS_CONTROLLER].reset();
		this.cleanAtDepth = this.setupWindow.controllers[ControllerNames.COMMANDS_CONTROLLER].depth();
	}
}
SessionController.ORDER = 1;
SessionController.prototype.ConfirmMessage = translate("Discard current map and start blank new map?");
SessionController.prototype.ConfirmTitle = translate("Confirmation");
SessionController.prototype.ConfirmButtonNo = translate("No");
SessionController.prototype.ConfirmButtonYes = translate("Yes");
