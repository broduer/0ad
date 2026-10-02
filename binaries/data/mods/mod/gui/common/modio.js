/**
 * Show the mod.io disclaimer and, if the player accepts it, open the mod.io page.
 *
 * @param {Object} [data] - Passed on to the mod.io page.
 * @param {string} [data.filter] - Text to prefill the search field with.
 * @returns {Promise<boolean>} Whether the page was opened. Resolves once it is closed.
 */
async function openModIo(data)
{
	initTerms({
		"Disclaimer": {
			"title": translate("Disclaimer"),
			"file": "gui/modio/Disclaimer.txt",
			"config": "modio.disclaimer",
			"accepted": false,
			"urlButtons": [
				{
					"caption": translate("mod.io Terms"),
					"url": "https://mod.io/terms"
				},
				{
					"caption": translate("mod.io Privacy Policy"),
					"url": "https://mod.io/privacy"
				}
			]
		}
	});

	await openTerms("Disclaimer");
	if (!g_Terms.Disclaimer.accepted)
		return false;

	await Engine.OpenChildPage("page_modio.xml", data);
	return true;
}
