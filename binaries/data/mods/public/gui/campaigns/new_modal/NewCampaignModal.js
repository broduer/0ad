/**
 * Modal screen that pops up when you start a new campaign from the setup screen.
 * asking you to name it.
 * Will then create the file with the according name and start everything up.
 */
class NewCampaignModal
{
	constructor(campaignTemplate, closePageCallback)
	{
		this.template = campaignTemplate;

		Engine.GetGUIObjectByName('cancelButton').onPress = closePageCallback;
		Engine.GetGUIObjectByName('startButton').onPress =
			this.createAndStartCampaign.bind(this, closePageCallback);
		Engine.GetGUIObjectByName('runDescription').caption = translateWithContext("Campaign Template", this.template.Name);
		Engine.GetGUIObjectByName('runDescription').onTextEdit = () =>
		{
			Engine.GetGUIObjectByName('startButton').enabled = Engine.GetGUIObjectByName('runDescription').caption.length > 0;
		};
		Engine.GetGUIObjectByName('runDescription').focus();
	}

	createAndStartCampaign(closePageCallback)
	{
		const filename = this.template.identifier + "_" + Date.now() + "_" + Math.floor(Math.random()*100000);
		const run = new CampaignRun(filename)
			.setTemplate(this.template)
			.setMeta(Engine.GetGUIObjectByName('runDescription').caption)
			.save()
			.setCurrent();

		closePageCallback({
			"page": run.getMenuPath(),
			"argument": {
				"filename": run.filename
			}
		});
	}
}


var g_NewCampaignModal;

function init(campaign_template_data)
{
	return new Promise(closePageCallback =>
	{
		registerGlobalGuiPageHotkeys(["options", "hotkeys", "civinfo", "structree", "catafalque", "mapbrowser", "manual", "tips"]);
		g_NewCampaignModal = new NewCampaignModal(campaign_template_data, closePageCallback);
	});
}
