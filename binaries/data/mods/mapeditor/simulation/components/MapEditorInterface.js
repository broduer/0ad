class MapEditorInterface
{
	Serialize()
	{
		return {};
	}

	Deserialize(data)
	{
		this.Init();
	}

	Init()
	{
		this.fn = new MapEditorFunctions();
	}

	ScriptCall(name, args)
	{
		// Todo validate this is a map editor game
		if (this.fn[name])
			return this.fn[name](args);

		throw new Error("Invalid MapEditorInterface Call name \"" + name + "\"");
	}
}

MapEditorInterface.prototype.Schema = "<a:component type='system'/><empty/>";

Engine.RegisterSystemComponentType(IID_MapEditorInterface, "MapEditorInterface", MapEditorInterface);
