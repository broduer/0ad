let g_AboutPage;

class AboutPage
{
	constructor(data, closePageCallback)
	{
		this.closePageCallback = closePageCallback;
		this.aboutInformation = Engine.GetGUIObjectByName("aboutInformation");
		this.closeButton = Engine.GetGUIObjectByName("closeButton");

		this.aboutInformation.caption =
			this.label(translate("Release:")) + " " + escapeText(data.release || translate("Not available")) + "\n" +
			this.label(translate("Version:")) + " " + escapeText(data.version || translate("Not available")) + "\n" +
			this.label(translate("Build date:")) + " " + escapeText(data.buildDate || translate("Not available")) + "\n\n" +
			this.label(translate("Loaded mods:")) + "\n" + this.formatMods(data.mods);

		this.closeButton.onPress = this.close.bind(this);
	}

	label(value)
	{
		return coloredText(value, "orange");
	}

	formatMods(mods)
	{
		if (!Array.isArray(mods) || !mods.length)
			return translate("Not available");

		return mods.map(mod =>
		{
			const name = mod.name || mod.mod || translate("Unknown");
			return escapeText(mod.version ? name + " " + mod.version : name);
		}).join("\n");
	}

	close()
	{
		this.closePageCallback();
	}
}

function init(data = {})
{
	return new Promise(closePageCallback =>
	{
		g_AboutPage = new AboutPage(data, closePageCallback);
	});
}
