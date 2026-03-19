var g_TipsPage;

function init(initData, hotloadData)
{
	return new Promise(closePageCallback =>
	{
		g_TipsPage = new TipsPage(initData, hotloadData, closePageCallback);
		Engine.SetGlobalHotkey("tips", "Press", closePageCallback);
	});
}

function getHotloadData()
{
	return g_TipsPage?.tipDisplay.getHotloadData();
}
