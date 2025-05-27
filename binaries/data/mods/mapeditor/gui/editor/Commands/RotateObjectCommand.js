commandManagerComands.RotateObject = class extends Command
{
	constructor(args)
	{
		super();
		this.entitiesId = args.entities;
		this.position = args.position;
		this.entitiesCmp = undefined;
	}

	getType()
	{
		return "RotateObject";
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
			MapEditor.MapEditorInterfaceCall("UpdateObjectPosition", {
				"id": cmpInfo.id,
				"x": cmpInfo.position.position.x,
				"z": cmpInfo.position.position.z,
				"rotation": cmpInfo.position.rotation,
			});
		}
	}

	redo()
	{
		for (const cmpInfo of this.entitiesCmp)
		{
			if (!cmpInfo.position)
				continue;

			const angle = Math.atan2(this.position.x - cmpInfo.position.position.x, this.position.z- cmpInfo.position.position.z);
			MapEditor.MapEditorInterfaceCall("UpdateObjectPosition", {
				"id": cmpInfo.id,
				"x": cmpInfo.position.position.x,
				"z": cmpInfo.position.position.z,
				"angle": angle,
			});
		}
	}

	mergeWith(command)
	{
		command.position = this.position;
	}
};
