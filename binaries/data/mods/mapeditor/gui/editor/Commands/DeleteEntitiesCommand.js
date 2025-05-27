commandManagerComands.DeleteEntities = class extends Command
{
	constructor(args)
	{
		super();
		this.finalize = true; // not mergeable
		this.entitiesId = args.entities;
		this.entitiesCmp = undefined;
	}

	getType()
	{
		return "DeleteEntities";
	}

	execute()
	{
		const result = MapEditor.MapEditorInterfaceCall("GetCmpInfo", { "entities": this.entitiesId });
		if (!result.success)
		{
			warn("Failed to get component info for entities");
			return;
		}
		this.entitiesCmp = result.data;
		this.redo();
	}

	undo()
	{
		if (this.entitiesCmp === undefined)
			return;

		for (const cmpInfo of this.entitiesCmp)
		{
			if (!MapEditor.CreateEntityWithId(cmpInfo.id, cmpInfo.template))
			{
				warn("Failed to create entity with id " + cmpInfo.id);
				continue;
			}

			MapEditor.MapEditorInterfaceCall("CreateObject", [{
				"id": cmpInfo.id,
				"x": cmpInfo.position.position.x,
				"z": cmpInfo.position.position.z,
				"rotation": cmpInfo.position.rotation,
				"template": cmpInfo.template,
				"player": cmpInfo.ownership,
				"seed": cmpInfo.visual.seed
			}]);
		}
	}

	redo()
	{
		MapEditor.MapEditorInterfaceCall("DeleteObjects", { "entities": this.entitiesId });
	}
};
