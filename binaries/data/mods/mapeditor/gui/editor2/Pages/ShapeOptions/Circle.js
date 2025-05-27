export const Circle = {
	"name": translateWithContext("Map Editor", "Circle"),
	"tooltip": translateWithContext("Map Editor", "Circle brush"),
	"onChange": function(size)
	{
		const data = [];
		const mid_x = size-1;
		const mid_y = size-1;

		for (let y = 0; y < size; ++y)
		{
			for (let x = 0; x < size; ++x)
			{
				const distance = ((2*x - mid_x) * (2*x - mid_x) + (2*y - mid_y) * (2*y - mid_y)) / (size * size * 1.0);
				if (distance < 1.0)
					data.push((Math.sqrt(2.0 - distance) - 1.0) / (Math.sqrt(2.0) - 1.0));
				else
					data.push(0.0);
			}
		}

		return data;
	}
};
