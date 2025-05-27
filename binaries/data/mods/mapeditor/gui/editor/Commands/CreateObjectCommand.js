commandManagerComands.CreateObject = class extends Command
{
	constructor(args)
	{
		super();
		this.cmdArgs = args;
		this.finalize = true; // not mergeable
		this.entitiesId = null;
	}

	getType()
	{
		return "CreateObject";
	}

	execute()
	{
		this.redo();
	}

	undo()
	{
		if (!this.entitiesId)
			return;

		MapEditor.MapEditorInterfaceCall("DeleteObjects", { "entities": this.entitiesId });
	}

	redo()
	{
		this.entitiesId = MapEditor.MapEditorInterfaceCall("CreateObject", this.cmdArgs).entitiesId;
	}
};
