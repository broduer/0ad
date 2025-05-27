export class LoadingOverlay
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.overlay = Engine.GetGUIObjectByName("loadingOverlay");
		this.progressbar = Engine.GetGUIObjectByName("progressbar");
		this.progressbar.onGameLoadProgress = this.onGameLoadProgress.bind(this);
		this.progressText = Engine.GetGUIObjectByName("progressText");
		this.progressTitle = Engine.GetGUIObjectByName("loadingTitle");

		this.setupWindow.registerGameLoadHandler(()=>
		{
			this.overlay.hidden = true;
		});
		this.setupWindow.registerGameUnloadHandler(()=>
		{
			this.progressTitle.caption = this.setupWindow.controllers.sessionController.phase;
			this.progressbar.progress = 0;
			this.overlay.hidden = false;
		});
	}

	onGameLoadProgress(progression, description)
	{
		if (progression >= 100) return;
		this.progressbar.progress = progression + 1;
		this.progressText.caption = description;
	}
}
