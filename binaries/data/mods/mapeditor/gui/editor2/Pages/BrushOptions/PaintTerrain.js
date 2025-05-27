import { StateNames } from "gui/editor2/Constants/ToolsAtlasController.js";

export const PaintTerrain = {
	"name": translateWithContext("Map Editor", "Paint Terrain"),
	"tooltip": translateWithContext("Map Editor", "Brush with left mouse button to paint texture dominantly,\nright mouse button to paint submissively.\nShift-left-click for eyedropper tool"),
	"state": StateNames.PAINT_TERRAIN,
	"order": 0
};

export const ReplaceTerrain = {
	"name": translateWithContext("Map Editor", "Replace Terrain"),
	"tooltip": translateWithContext("Map Editor", "Replace all of a terrain texture with a new one"),
	"state": StateNames.REPLACE_TERRAIN,
	"order": 1
};

export const FillTerrain = {
	"name": translateWithContext("Map Editor", "Fill Terrain"),
	"tooltip": translateWithContext("Map Editor", "Bucket fill a patch of terrain texture with a new one"),
	"state": StateNames.FILL_TERRAIN,
	"order": 2
};
