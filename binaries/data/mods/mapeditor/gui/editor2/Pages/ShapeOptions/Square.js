export const Square = {
	"name": translateWithContext("Map Editor", "Square"),
	"tooltip": translateWithContext("Map Editor", "Square brush"),
	"onChange": function(size)
	{
		const data = [];
		for (let y = 0; y < size; ++y)
		{
			for (let x = 0; x < size; ++x)
			{
				data.push(1);
			}
		}

		return data;
	}
};
