class Subsection
{
	constructor(page)
	{
		this.page = page;
	}

	getAuraCaptions(auraList, civCode)
	{
		const captions = [];
		for (const auraCode of auraList)
		{
			const aura = this.page.TemplateParser.getAura(auraCode);

			captions.push(this.page.formatEntry(
				getEntityNames(aura),
				false,
				getDescriptionTooltip(aura)
			));
		}
		return captions;
	}

	getEntityCaptions(entityList, classList, civCode)
	{
		const captions = [];
		for (const entityCode of entityList)
		{
			// Acquire raw template as we need to compare all classes an entity has, not just the visible ones.
			const template = this.page.TemplateLoader.loadEntityTemplate(entityCode, civCode);
			const classListFull = GetIdentityClasses(template.Identity);
			if (!MatchesClassList(classListFull, classList))
				continue;

			const entity = this.page.TemplateParser.getEntity(entityCode, civCode);
			captions.push(this.page.formatEntry(
				getEntityNames(entity),
				getDescriptionTooltip(entity),
				getEntityTooltip(entity)
			));
		}
		return captions;
	}

	getTechnologyCaptions(technologyList, civCode)
	{
		const captions = [];
		for (const techCode of technologyList)
		{
			const technology = this.page.TemplateParser.getTechnology(techCode, civCode);
			if (technology.replaces !== undefined && technology.replaces.length === 1)
			{
				const replacedTechnology = this.page.TemplateParser.getTechnology(technology.replaces[0], "anyciv");
				if (technology.name.generic === replacedTechnology.name.generic)
				{
					// This tech is a drop-in replacement for a generic tech to e.g. work around unique requirements.
					// So, don't include in "specific technologies"
					continue;
				}
			}
			// We deliberately pass an invalid civ code here.
			// If it returns with a value other than false, then
			// we know that this tech can be researched by any civ
			const genericReqs = this.page.TemplateParser.getTechnology(techCode, "anyciv").reqs;

			if (!technology.reqs || genericReqs)
				continue;

			captions.push(this.page.formatEntry(
				getEntityNames(technology),
				getDescriptionTooltip(technology),
				getEntityTooltip(technology)
			));
		}
		return captions;
	}
}
