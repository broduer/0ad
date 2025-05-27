objectTools.Entities = {
	"name": translateWithContext("Map Editor", "Entities"),
	"onChange": function(filter)
	{
		const entities = MapEditor.GetTemplates("simulation");

		const filterEntities = filter && filter !== "" ? entities.filter((entity) => entity.toLowerCase().includes(filter.toLowerCase())) : entities;
		return {
			"labels": filterEntities,
			"values": filterEntities
		};
	},
	"state": "objectPreview"
};

objectTools.Actors = {
	"name": translateWithContext("Map Editor", "Actors"),
	"onChange": function(filter)
	{
		const actors = MapEditor.GetTemplates("actor");
		const filterActors = filter && filter !== "" ? actors.filter((actor) => actor.toLowerCase().includes(filter.toLowerCase())) : actors;
		const formatActors = filterActors.map((entity) => entity.substr(6));
		return {
			"labels": formatActors,
			"values": filterActors
		};
	},
	"state": "objectPreview"
};
