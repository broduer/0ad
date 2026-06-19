/**
 * This class is concerned with managing the different elements of the minimap panel.
 */
class MiniMapPanel
{
	constructor(playerViewControl, diplomacyColors, idleWorkerClasses)
	{
		this.loadConfig();
		this.panel = Engine.GetGUIObjectByName("minimapPanel");
		this.minimapBackgroundTexture = Engine.GetGUIObjectByName("minimapBackgroundTexture");
		this.layoutSquare = Engine.GetGUIObjectByName("minimapLayoutSquare");
		this.layoutRound = Engine.GetGUIObjectByName("minimapLayoutRound");
		this.layoutExpanded = Engine.GetGUIObjectByName("minimapLayoutExpanded");
		this.session = Engine.GetGUIObjectByName("session");
		this.idleWorkerButton = new MiniMapIdleWorkerButton(playerViewControl, idleWorkerClasses);
		this.flareButton = new MiniMapFlareButton(playerViewControl);
		this.miniMap = new MiniMap();
		this.diplomacyColorsButton = new MiniMapDiplomacyColorsButton(diplomacyColors);
		this.supplementalSelectionDetails = Engine.GetGUIObjectByName("supplementalSelectionDetails");
		const panelRect = this.panel.getComputedSize();
		this.defaultPanelWidth = panelRect.right - panelRect.left;
		this.defaultPanelHeight = panelRect.bottom - panelRect.top;
		playerViewControl.registerViewedPlayerChangeHandler(this.rebuild.bind(this));
		registerHotkeyChangeHandler(this.rebuild.bind(this));
		registerConfigChangeHandler(this.onConfigChange.bind(this));
		Engine.SetGlobalHotkey("session.gui.expand.toggle", "Press", this.toggleExpanded.bind(this));
		this.expanded = false;
		this.applyLayout();
		this.panel.onWindowResized = this.applyLayout.bind(this);
	}

	loadConfig()
	{
		this.position = Engine.ConfigDB_GetValue("user", "gui.session.minimap.position");
		this.shape = Engine.ConfigDB_GetValue("user", "gui.session.minimap.shape") || "square";
		this.sizeScale = parseFloat(Engine.ConfigDB_GetValue("user", "gui.session.minimap.size")) || 1.0;
	}

	toggleExpanded()
	{
		this.expanded = !this.expanded;
		this.applyLayout();
	}

	onConfigChange(changes)
	{
		if (![...changes].some(change =>
			change == "gui.session.minimap.position" ||
			change == "gui.session.minimap.shape" ||
			change == "gui.session.minimap.size"))
			return;
		this.loadConfig();
		this.applyLayout();
	}

	applyLayout()
	{
		const scale = this.getEffectiveScale();
		this.panel.absolute = this.expanded || this.position !== "panel-left";
		this.panel.size = this.expanded ? this.computeExpandedLayout() : this.computeNormalLayout(scale);
		this.panel.sprite = !this.expanded && this.shape == "square" ? "mapPanel" : "";
		this.layoutSquare.hidden = this.expanded || this.shape != "square";
		this.layoutRound.hidden = this.expanded || this.shape != "round";
		this.layoutExpanded.hidden = !this.expanded;
		this.flareButton.updateSprites(g_IsObserver);
	}

	getEffectiveScale()
	{
		return Math.min(this.sizeScale, this.supplementalSelectionDetails.getComputedSize().left / this.defaultPanelWidth);
	}

	getScaledPanelSize(scale)
	{
		return {
			"width": Math.round(this.defaultPanelWidth * scale),
			"height": Math.round(this.defaultPanelHeight * scale)
		};
	}

	computeNormalLayout(scale)
	{
		return this.position === "panel-left" ?
			this.computePanelLeftLayout(scale) :
			this.computeScreenLeftLayout(scale);
	}

	computePanelLeftLayout(scale)
	{
		const { width, height } = this.getScaledPanelSize(scale);
		const parentRect = this.panel.parent.getComputedSize();
		const parentHeight = parentRect.bottom - parentRect.top;
		return {
			"left": this.defaultPanelWidth - width,
			"top": parentHeight - height,
			"right": this.defaultPanelWidth,
			"bottom": parentHeight
		};
	}

	computeScreenLeftLayout(scale)
	{
		const { width, height } = this.getScaledPanelSize(scale);
		return {
			"rleft": 0,
			"left": 0,
			"rright": 0,
			"right": width,
			"rtop": 100,
			"top": -height,
			"rbottom": 100,
			"bottom": 0
		};
	}

	computeExpandedLayout()
	{
		const windowSize = this.session.getComputedSize();
		const scale = 0.55;
		const marginLeft = 20;
		const verticalOffset = -40;
		const screenHeight = windowSize.bottom - windowSize.top;
		const size = Math.floor(screenHeight * scale);
		const screenTop = Math.floor((screenHeight - size) / 2) + verticalOffset;
		return {
			"left": marginLeft,
			"top": screenTop,
			"right": marginLeft + size,
			"bottom": screenTop + size
		};
	}

	flare(target, playerID)
	{
		return this.miniMap.flare(target, playerID);
	}

	isMouseOverMiniMap()
	{
		return this.miniMap.isMouseOverMiniMap();
	}

	rebuild()
	{
		this.applyLayout();
		this.setCivBackgroundTexture();
	}

	setCivBackgroundTexture()
	{
		const playerCiv = g_ViewedPlayer > 0 ? g_Players[g_ViewedPlayer].civ : "gaia";
		this.minimapBackgroundTexture.sprite = `stretched:session/icons/bkg/background_circle_${playerCiv}.png`;
	}
}
