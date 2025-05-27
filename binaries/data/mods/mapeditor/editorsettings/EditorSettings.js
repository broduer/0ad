class EditorSettings extends BaseSettings
{
	init(mapEditorCache)
	{
		super.init();

		if (!mapEditorCache)
			mapEditorCache = new MapEditorCache();

		mapEditorCache.setSettings(this);

		this.mapEditorCache = mapEditorCache;
	}

	exceptObjectKeys()
	{
		return ["mapEditorCache", "setupWindow"];
	}

	setupInitAttributesObject()
	{
		return { "settings": {} };
	}
}
