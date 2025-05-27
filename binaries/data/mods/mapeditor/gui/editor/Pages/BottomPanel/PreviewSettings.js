EditorWindow.prototype.ClassSetupWindowPages.PreviewSettings = class
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.previewSettingsGUI = Engine.GetGUIObjectByName("previewSettings");
		this.playerPreviewSetting = Engine.GetGUIObjectByName("playerPreviewSetting");

		this.setupWindow.registerLoadHandler(()=>
		{
			this.guiController = this.setupWindow.controls.guiController;
			this.objectSettings = this.setupWindow.controls.objectSettings;
			this.editorPlayerGeneral = this.setupWindow.controls.editorSettings.editorPlayerGeneral;

			this.editorPlayerGeneral.watch(() => this.init(), ["playerNumber"]);

			this.objectSettings.watch(() =>
			{
				const indexSelected = this.playerPreviewSetting.list_data.indexOf(this.objectSettings.playerId+"");
				this.playerPreviewSetting.selected = indexSelected;
			}, ["playerId"]);

			this.guiController.watch(()=>
			{
				this.previewSettingsGUI.hidden = !this.guiController.objectSettingsIsOpen;
			}, ["objectSettingsIsOpen"]);

			this.init();
		});
	}

	init()
	{
		const playerNumber = this.editorPlayerGeneral.playerNumber;

		const players = [...Array(playerNumber + 1).keys()];
		const playersName = players.map((player) =>
		{
			if (player === 0)
				return this.editorPlayerGeneral.getPropertyDefault(player, "Name");
			return this.editorPlayerGeneral.playerData[player]?.name || this.editorPlayerGeneral.getPropertyDefault(player, "Name") || "undefined";
		});

		this.playerPreviewSetting.list = playersName;
		this.playerPreviewSetting.list_data = players;

		this.playerPreviewSetting.onSelectionChange = () =>
		{
			this.objectSettings.playerId = parseInt(this.playerPreviewSetting.list_data[this.playerPreviewSetting.selected]);
		};

		this.playerPreviewSetting.selected = 0;
	}
};
