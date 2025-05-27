import { StateNames } from "gui/editor2/Constants/ToolsAtlasController.js";

export const AlterElevation = {
	"name": translateWithContext("Map Editor", "Modify Elevation"),
	"tooltip": translateWithContext("Map Editor", "Brush with left mouse buttons to raise terrain,\nright mouse button to lower it"),
	"state": StateNames.ALTER_ELEVATION
};

export const PickElevation = {
	"name": translateWithContext("Map Editor", "Ridge Elevation"),
	"tooltip": translateWithContext("Map Editor", "Brush with left mouse buttons to raise terrain,\nright mouse button to lower it"),
	"state": StateNames.PICK_ELEVATION
};

export const SmoothElevation = {
	"name": translateWithContext("Map Editor", "Smooth Elevation"),
	"tooltip": translateWithContext("Map Editor", "Brush with left mouse button to smooth terrain,\nright mouse button to roughen it"),
	"state": StateNames.SMOOTH_ELEVATION
};

export const FlattenElevation = {
	"name": translateWithContext("Map Editor", "Flatten Elevation"),
	"tooltip": translateWithContext("Map Editor", "Brush with left mouse button to flatten terrain"),
	"state": StateNames.FLATTEN_ELEVATION
};
