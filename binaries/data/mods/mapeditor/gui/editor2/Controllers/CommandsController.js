import { CommandProc } from "gui/global/CommandPattern.js";
import { commands } from "gui/editor2/Commands/index.js";
import { GUI_MAPPINGS_EVENTS, GUI_MAPPINGS_HOTKEYS } from "gui/global/SDLConstants.js";

export class CommandsController
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.cmdProc = new CommandProc();

		this.setupWindow.registerHandleInputAfterGuiHandler(this.onHandleInputAfterGui.bind(this));
	}

	depth()
	{
		return this.cmdProc.historyCommand.length;
	}

	reset()
	{
		this.cmdProc.historyCommand = [];
	}

	pushCommand(commandName, args)
	{
		if (commands[commandName] === undefined)
			throw new Error('Invalid command name ' + commandName);

		this.cmdProc.submit(new commands[commandName](args));
		if (this.cmdProc.historyCommand.length > 0)
			return this.cmdProc.historyCommand[this.cmdProc.historyCommand.length - 1];
		return null;
	}

	markCommandAsFinalized()
	{
		if (this.cmdProc.historyCommand.length > 0)
			this.cmdProc.historyCommand[this.cmdProc.historyCommand.length - 1].finalize = true;
	}

	onHandleInputAfterGui(ev)
	{
		if (ev.type === GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey === GUI_MAPPINGS_HOTKEYS.MAPEDITOR_UNDO)
		{
			this.cmdProc.undo();
			return true;
		}
		else if (ev.type === GUI_MAPPINGS_EVENTS.SDL_HOTKEYPRESS && ev.hotkey === GUI_MAPPINGS_HOTKEYS.MAPEDITOR_REDO)
		{
			this.cmdProc.redo();
			return true;
		}
		return false;
	}
}
CommandsController.ORDER = 0;
