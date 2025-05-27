function PreInitGame()
{
	// Explore the map inside the players' territory borders
	const cmpRangeManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_RangeManager);
	cmpRangeManager.ExploreTerritories();
}

function InitGame()
{
	// Update the grid with all entities created for the map init.
	Engine.QueryInterface(SYSTEM_ENTITY, IID_Pathfinder).UpdateGrid();
}

Engine.RegisterGlobal("PreInitGame", PreInitGame);
Engine.RegisterGlobal("InitGame", InitGame);
