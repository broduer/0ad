import { SessionController } from "gui/editor2/Controllers/SessionController.js";
import { CommandsController } from "gui/editor2/Controllers/CommandsController.js";
import { ToolsAtlasController } from "gui/editor2/Controllers/ToolsAtlasController.js";
import { GuiController } from "gui/editor2/Controllers/GuiController.js";
import { BrushSettingsController } from "gui/editor2/Controllers/BrushSettingsController.js";

/**
 * Runtime keys are the class name with a lowercased first letter,
 * matching the values in Constants/Controllers.js.
 * @typedef {Object} ControllerMap
 * @property {CommandsController} commandsController
 * @property {SessionController} sessionController
 * @property {ToolsAtlasController} toolsAtlasController
 * @property {GuiController} guiController
 * @property {BrushSettingsController} brushSettingsController
 */

export const controllers = {
	CommandsController,
	SessionController,
	ToolsAtlasController,
	GuiController,
	BrushSettingsController,
};
