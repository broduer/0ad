export function layoutHorizontal(container, padding = 5, start = 0)
{
	let offset = start;
	for (let i = 0; i < container.children.length; i++)
	{
		const child = container.children[i];
		if (child.hidden)
			continue;
		const size = child.size;
		const width = size.right - size.left;
		size.left = offset;
		size.right = offset + width;
		child.size = size;
		offset = size.right + padding;
	}

	return offset;
}

export function layoutVertical(container, padding = 5, start = 0)
{
	let offset = start;
	for (let i = 0; i < container.children.length; i++)
	{
		const child = container.children[i];
		if (child.hidden)
			continue;
		const size = child.size;
		const height = size.bottom - size.top;
		size.top = offset;
		size.bottom = offset + height;
		child.size = size;
		offset = size.bottom + padding;
	}

	return offset;
}
