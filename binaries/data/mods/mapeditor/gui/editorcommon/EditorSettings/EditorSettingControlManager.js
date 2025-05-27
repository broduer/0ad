/**
 * The EditorSettingControlManager owns all GUI controls.
 */
class EditorSettingControlManager
{
	constructor(setupWindow)
	{
		this.setupWindow = setupWindow;

		this.rows = {};
		this.editorSettingControls = {};

		for (const name in this.ClassEditorSettingControls)
			this.editorSettingControls[name] =
				new this.ClassEditorSettingControls[name](this);
	}

	getNextRow(name)
	{
		if (this.rows[name] === undefined)
			this.rows[name] = 0;
		else
			++this.rows[name];

		return this.rows[name];
	}

	updateSettingVisibility()
	{
		for (const name in this.editorSettingControls)
			this.editorSettingControls[name].updateVisibility();
	}

	hideSettingByName(fnName)
	{
		for (const name in this.editorSettingControls)
		{
			this.editorSettingControls[name].hidden = fnName(name);
			this.editorSettingControls[name].updateVisibility();
		}
	}

	addAutocompleteEntries(entries)
	{
		for (const name in this.editorSettingControls)
			this.editorSettingControls[name].addAutocompleteEntries(name, entries);
	}
}

Object.defineProperty(EditorSettingControlManager.prototype, "ClassEditorSettingControls", {
	"value": {},
	"enumerable": false,
	"writable": true,
});
