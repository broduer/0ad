EditorSettings.prototype.Attributes.EditorMapGeneral = class extends EditorSetting
{
	init()
	{
		this.circularMap = false;
		this.description = undefined;
		this.name = undefined;
		this.preview = undefined;
		this.revealMap = false;
		this.seed = 0;
		this.aiSeed = 0;
	}

	toInitAttributes(attribs)
	{
		attribs.settings.CircularMap = this.circularMap;
		attribs.settings.Description = this.description;
		attribs.settings.Name = this.name;
		attribs.settings.Preview = this.preview;
		attribs.settings.RevealMap = this.revealMap;
		attribs.settings.AISeed = this.aiSeed;
		attribs.settings.Seed = this.seed;
	}

	fromInitAttributes(attribs)
	{
		this.setCircularMap(!!attribs.settings.CircularMap);
		this.setDescription(attribs.settings.Description);
		this.setName(attribs.settings.Name);
		this.setPreview(attribs.settings.Preview);
		this.setRevealMap(!!attribs.settings.RevealMap);
		this.setAISeed(attribs.settings.AISeed);
		this.setSeed(attribs.settings.Seed);
	}

	setCircularMap(circularMap)
	{
		this.circularMap = circularMap;
	}

	setDescription(description)
	{
		this.description = description;
	}

	setName(name)
	{
		this.name = name;
	}

	setPreview(preview)
	{
		this.preview = preview;
	}

	setRevealMap(revealMap)
	{
		this.revealMap = revealMap;
	}

	setAISeed(aiSeed)
	{
		this.aiSeed = aiSeed || 0;
	}

	setSeed(seed)
	{
		this.seed = seed || 0;
	}
};

