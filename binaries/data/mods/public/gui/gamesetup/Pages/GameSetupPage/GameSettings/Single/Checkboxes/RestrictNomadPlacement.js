GameSettingControls.RestrictNomadPlacement = class RestrictNomadPlacement extends GameSettingControlCheckbox
{
	constructor(...args)
	{
		super(...args);
		g_GameSettings.restrictNomadPlacement.watch(() => this.render(), ["enabled"]);
		g_GameSettings.nomad.watch(() => this.render(), ["enabled"]);
		g_GameSettings.map.watch(() => this.render(), ["type"]);
		this.render();
	}

	render()
	{
		this.setHidden(g_GameSettings.map.type != "random" || !g_GameSettings.nomad.enabled);
		this.setChecked(g_GameSettings.restrictNomadPlacement.enabled);
	}

	onPress(checked)
	{
		g_GameSettings.restrictNomadPlacement.setEnabled(checked);
		this.gameSettingsController.setNetworkInitAttributes();
	}
};

GameSettingControls.RestrictNomadPlacement.prototype.TitleCaption =
	translate("Nomad Spacing");

GameSettingControls.RestrictNomadPlacement.prototype.Tooltip =
	translate("Disable placement of a civic center within 200m of an already placed civic center");
