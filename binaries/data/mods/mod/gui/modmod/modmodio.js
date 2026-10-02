export async function downloadModsButton(initMods)
{
	if (await openModIo())
		initMods();
}
