class MapEditorFunctions
{
	/**
	* Display object preview.
	* cmd.template is the name of the object template, or "" to disable the preview.
	* cmd.x, cmd.z, cmd.angle give the location.
	* cmd.actorSeed is a random seed for the actor.
	* cmd.player is the player owner of the object.
	* cmd.cleanObjectPreviews is a boolean to clean all previews.
	*
	* Returns result object from CheckPlacement:
	* 	{
	*		"success":             true iff the placement is valid, else false
	*		"message":             message to display in UI for invalid placement, else ""
	*		"parameters":          parameters to use in the message
	*		"translateMessage":    localisation info
	*		"translateParameters": localisation info
	*		"pluralMessage":       we might return a plural translation instead (optional)
	*		"pluralCount":         localisation info (optional)
	*  }
	*/
	SetObjectPreview(cmd)
	{
		const result = {
			"success": true,
			"message": "",
			"parameters": {},
			"translateMessage": false,
			"translateParameters": []
		};

		cmd.cleanObjectPreviews = cmd.cleanObjectPreviews ?? true;

		if (!this.objectPreview || this.objectPreview[this.objectPreview.length - 1].template !== cmd.template || !cmd.cleanObjectPreviews)
		{
			if (this.objectPreview && cmd.cleanObjectPreviews)
			{
				for (const objectPreview of this.objectPreview)
					Engine.DestroyEntity(objectPreview.id);
				this.objectPreview = undefined;
			}

			if (cmd.template == "")
				this.objectPreview = undefined;
			else
				this.objectPreview = [...(this.objectPreview ?? []), { "template": cmd.template, "id": Engine.AddLocalEntity("atlas|" + cmd.template) }];
		}

		if (this.objectPreview)
		{
			const ent = this.objectPreview[this.objectPreview.length - 1].id;

			const pos = Engine.QueryInterface(ent, IID_Position);
			if (pos)
			{
				pos.JumpTo(cmd.x, cmd.z);
				pos.SetYRotation(cmd.angle);
			}

			if (cmd.player)
			{
				const ownership = Engine.QueryInterface(ent, IID_Ownership);
				if (ownership)
					ownership.SetOwner(cmd.player);
			}

			const cmpVisual = Engine.QueryInterface(ent, IID_Visual);
			if (cmpVisual)
			{
				if (cmd.actorSeed)
					cmpVisual.SetActorSeed(cmd.actorSeed);

				let shadingColor = [1.0, 1.0, 1.0, 1.0];
				const cmpObstruction = Engine.QueryInterface(ent, IID_Obstruction);
				if (cmpObstruction && cmpObstruction.CheckFoundation("default") !== "success")
					shadingColor = [1.4, 0.4, 0.4, 1];

				cmpVisual.SetShadingColor(...shadingColor);
			}
		}

		return result;
	}

	/**
	* Create objects.
	* cmds array of object to create
	* cmd.template is the name of the object template, or "" to disable the preview.
	* cmd.x, cmd.z, cmd.angle give the location.
	* cmd.actorSeed is a random seed for the actor.
	* cmd.player is the player owner of the object.
	*
	* Returns result object from CheckPlacement:
	* 	{
	*		"success":             true iff the placement is valid, else false
	*		"message":             message to display in UI for invalid placement, else ""
	*		"parameters":          parameters to use in the message
	*		"translateMessage":    localisation info
	*		"translateParameters": localisation info
	*		"pluralMessage":       we might return a plural translation instead (optional)
	*		"pluralCount":         localisation info (optional)
	*		"entitiesId":            the IDs of the created entities
	*  }
	*/
	CreateObject(cmds)
	{
		const result = {
			"success": false,
			"message": "",
			"parameters": {},
			"translateMessage": false,
			"translateParameters": []
		};

		for (const cmd of cmds)
		{
			const ent = cmd.id ?? Engine.AddEntity(cmd.template);

			if (ent == INVALID_ENTITY)
				return result;

			const pos = Engine.QueryInterface(ent, IID_Position);
			if (pos)
			{
				pos.JumpTo(cmd.x, cmd.z);
				if (cmd.angle)
					pos.SetYRotation(cmd.angle);

				if (cmd.rotation)
				{
					pos.SetXZRotation(cmd.rotation.x, cmd.rotation.z);
					pos.SetYRotation(cmd.rotation.y);
				}
			}

			if (cmd.player)
			{
				const ownership = Engine.QueryInterface(ent, IID_Ownership);
				if (ownership)
					ownership.SetOwner(cmd.player);
			}

			const cmpVisual = Engine.QueryInterface(ent, IID_Visual);
			if (cmpVisual)
			{
				if (cmd.actorSeed)
					cmpVisual.SetActorSeed(cmd.actorSeed);

				let shadingColor = [1.0, 1.0, 1.0, 1.0];
				const cmpObstruction = Engine.QueryInterface(ent, IID_Obstruction);
				if (cmpObstruction && cmpObstruction.CheckFoundation("default") !== "success")
					shadingColor = [1.4, 0.4, 0.4, 1];

				cmpVisual.SetShadingColor(...shadingColor);
			}

			result.entitiesId = [...(result.entitiesId ?? []), ent];
		}
		result.success = true;
		return result;
	}

	DeleteObjects(cmd)
	{
		const result = {
			"success": true,
			"message": "",
			"parameters": {},
			"translateMessage": false,
			"translateParameters": []
		};

		for (const ent of cmd.entities)
			Engine.DestroyEntity(ent);

		return result;
	}

	HighlightSelectable(cmd)
	{
		const result = {
			"success": true,
			"message": "",
			"parameters": {},
			"translateMessage": false,
			"translateParameters": []
		};

		for (const ent of cmd.entities)
		{
			const cmpSelectable = Engine.QueryInterface(ent, IID_Selectable);
			if (!cmpSelectable)
				continue;

			cmpSelectable.SetSelectionHighlight({ "r": 1, "g": 1, "b": 1, "a": cmd.alpha }, cmd.selected);
			cmpSelectable.UpdateColor();
		}

		return result;
	}

	GetCmpInfo(cmd)
	{
		const result = {
			"success": false,
			"message": "",
			"parameters": {},
			"translateMessage": false,
			"translateParameters": []
		};

		if (cmd.entities && cmd.entities.length == 0)
			return result;

		result.data = [];
		const cmpTemplateManager = Engine.QueryInterface(SYSTEM_ENTITY, IID_TemplateManager);

		for (const ent of cmd.entities)
		{
			const cmpInfo = { "id": ent, "template": cmpTemplateManager.GetCurrentTemplateName(ent) };

			const pos = Engine.QueryInterface(ent, IID_Position);
			if (pos)
			{
				cmpInfo.position = {
					"position": pos.GetPosition(),
					"rotation": pos.GetRotation()
				};
			}

			const ownership = Engine.QueryInterface(ent, IID_Ownership);
			if (ownership)
				cmpInfo.ownership = ownership.GetOwner();

			const cmpVisual = Engine.QueryInterface(ent, IID_Visual);
			if (cmpVisual)
			{
				cmpInfo.visual = {
					"seed": cmpVisual.GetActorSeed(),
				};
			}

			result.data.push(cmpInfo);
		}

		result.success = true;
		return result;
	}

	UpdateObjectPosition(cmd)
	{
		const result = {
			"success": false,
			"message": "",
			"parameters": {},
			"translateMessage": false,
			"translateParameters": []
		};

		const pos = Engine.QueryInterface(cmd.id, IID_Position);
		if (!pos)
			return result;

		pos.JumpTo(cmd.x, cmd.z);

		if (cmd.angle)
			pos.SetYRotation(cmd.angle);

		if (cmd.rotation)
		{
			pos.SetXZRotation(cmd.rotation.x, cmd.rotation.z);
			pos.SetYRotation(cmd.rotation.y);
		}

		return result;
	}
}

Engine.RegisterGlobal("MapEditorFunctions", MapEditorFunctions);
