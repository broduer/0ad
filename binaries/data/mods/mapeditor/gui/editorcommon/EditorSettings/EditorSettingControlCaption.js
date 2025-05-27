/**
 * This class is implemented by editor settings that are controlled by a caption.
 */
class EditorSettingControlCaption extends EditorSettingControl
{
	constructor(...args)
	{
		super(...args);
	}

	setControl(editorSettingControlManager)
	{
		const row = editorSettingControlManager.getNextRow("captionSettingFrame");
		this.frame = Engine.GetGUIObjectByName("captionSettingFrame[" + row + "]");

		this.title = this.frame.children[0];
	}

	setControlTooltip(tooltip)
	{
		this.title.tooltip = tooltip;
	}

	setControlHidden(hidden)
	{
		this.title.hidden = hidden;
	}
}
