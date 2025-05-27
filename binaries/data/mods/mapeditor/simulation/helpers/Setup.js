function LoadMapSettings(settings)
{
	const cmpRangeManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_RangeManager);
	if (cmpRangeManager)
		cmpRangeManager.SetLosCircular(!!settings.CircularMap);

	const cmpObstructionManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_ObstructionManager);
	if (cmpObstructionManager)
		cmpObstructionManager.SetPassabilityCircular(!!settings.CircularMap);

}

Engine.RegisterGlobal("LoadMapSettings", LoadMapSettings);
