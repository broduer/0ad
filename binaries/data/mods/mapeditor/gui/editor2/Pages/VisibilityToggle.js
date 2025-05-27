import { layoutHorizontal } from "gui/global/Layouts.js";

export class VisibilityTogglePage
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;
		this.container = Engine.GetGUIObjectByName("panelToggles");
		this.toogleIndex = [true, true, true];
		this.leftPanelGUI = Engine.GetGUIObjectByName("leftPanel");
		this.bottomPanelGUI = Engine.GetGUIObjectByName("bottomPanel");
		this.rightPanelGUI = Engine.GetGUIObjectByName("rightPanel");
		this.minimapGUI = Engine.GetGUIObjectByName("minimapPanel");

		this.container.children[0].caption = this.LeftPanel;
		this.container.children[0].tooltip = this.LeftPanelTooltip;
		this.container.children[1].caption = this.BottomPanel;
		this.container.children[1].tooltip = this.BottomPanelTooltip;
		this.container.children[2].caption = this.RightPanel;
		this.container.children[2].tooltip = this.RightPanelTooltip;

		for (let i = 0; i < this.toogleIndex.length; i++)
		{
			this.container.children[i].onPress = () =>
			{
				this.toogleIndex[i] = !this.toogleIndex[i];
				switch (i)
				{
				case 0:
					this.leftPanelGUI.hidden = !this.toogleIndex[i];
					break;
				case 1:
					this.bottomPanelGUI.hidden = !this.toogleIndex[i];
					this.minimapGUI.hidden = !this.toogleIndex[i];
					break;
				default:
					this.rightPanelGUI.hidden = !this.toogleIndex[i];
					break;
				}
			};
		}

		layoutHorizontal(this.container);
	}
}

VisibilityTogglePage.prototype.LeftPanel = "L";
VisibilityTogglePage.prototype.LeftPanelTooltip = translate("Toggle Left Panel visibility");
VisibilityTogglePage.prototype.BottomPanelTooltip = translate("Toggle Bottom Panel visibility");
VisibilityTogglePage.prototype.RightPanelTooltip = translate("Toggle Right Panel visibility");
VisibilityTogglePage.prototype.BottomPanel = "B";
VisibilityTogglePage.prototype.RightPanel = "R";
