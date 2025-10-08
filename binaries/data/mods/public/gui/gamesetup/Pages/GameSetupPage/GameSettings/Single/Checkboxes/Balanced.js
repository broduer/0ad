GameSettingControls.Balanced = class Balanced extends GameSettingControlCheckbox
{
	constructor(...args)
	{
		super(...args);
		g_GameSettings.balanced.watch(() => this.render(), ["enabled"]);
		g_GameSettings.map.watch(() => this.render(), ["map"]);
		this.render();
	}

	render()
	{
		this.setHidden(!g_GameSettings.map.data?.settings.BalancedScript);
		this.setChecked(g_GameSettings.balanced.enabled);
	}

	onPress(checked)
	{
		g_GameSettings.balanced.setEnabled(checked);
		this.gameSettingsController.setNetworkInitAttributes();
	}
};

GameSettingControls.Balanced.prototype.TitleCaption =
	translate("Balanced");

GameSettingControls.Balanced.prototype.Tooltip =
	translate("Use the resource balancer to improve resource fairness for competetive play.");
