/**
 * This class manages the cinematic overlay, which is responsible for two things:
 *
 * 1. Show black bars at the top and bottom while cinema paths are playing. Cinema paths are predefined camera
 *  animations, which block player input of any kind while playing; cutscenes, essentially. Whether one is playing is
 *  communicated through the simulation state.
 *
 * 2. Display "speech". This could be narration or monologue/dialogue of a unit and can be displayed at any point during
 *  a match, including while cinema paths are playing. The speech UI includes the same two black bars, but also various
 *  additional elements inside the bottom one. Speech data is received from the simulation through GUI notifications and
 *  divided into "speech segments": each can contain multiple lines, but only specify one speaking unit (or none).
 */
class CinemaOverlay
{
	parentObject = Engine.GetGUIObjectByName("cinemaOverlay");
	barTop = Engine.GetGUIObjectByName("cinemaOverlayBarTop");
	barBottom = Engine.GetGUIObjectByName("cinemaOverlayBarBottom");
	speechDisplay = Engine.GetGUIObjectByName("cinemaOverlaySpeechDisplay");
	speechDisplayParent = Engine.GetGUIObjectByName("cinemaOverlaySpeechDisplayParent");
	speakingUnitDisplay = Engine.GetGUIObjectByName("cinemaOverlaySpeakingUnitDisplay");
	unitIcon = Engine.GetGUIObjectByName("cinemaOverlayUnitIcon");
	unitName = Engine.GetGUIObjectByName("cinemaOverlayUnitName");
	textContainer = Engine.GetGUIObjectByName("cinemaOverlayTextContainer");
	text = Engine.GetGUIObjectByName("cinemaOverlayText");

	// Objects to hide while showing the overlay.
	primarySessionOverlays = Engine.GetGUIObjectByName("primaryOverlays");
	bandbox = Engine.GetGUIObjectByName("bandbox");
	hotkeys = Engine.GetGUIObjectByName("hotkeys");

	speechSegments = [];
	activeSegmentIndex = -1;
	activeLineIndex = -1;
	loadNextLineTimer;

	isCutsceneModeEnabled = Engine.Renderer_GetCutsceneModeEnabled();

	constructor(hotloadData)
	{
		registerSimulationUpdateHandler(this.onSimulationUpdate.bind(this));
		this.parentObject.onWindowResized = this.recalculateBarSizes.bind(this);
		this.recalculateBarSizes();
		this.parentObject.hidden = true;

		if (hotloadData?.speechSegments?.length)
		{
			this.speechSegments = hotloadData.speechSegments;
			this.activeSegmentIndex = hotloadData.activeSegmentIndex;
			this.activeLineIndex = hotloadData.activeLineIndex;
			this.show();
			this.loadActiveLine(null);
		}
		else
		{
			// Not all cutscenes have speech data to display.
			// Until we receive a notification for it, we only want to display the two black bars.
			this.setSpeechDisplayHidden(true);
			this.setSpeakingUnit(undefined);
		}
	}

	/**
	 * Enable or disable cutscene mode and remember it in order to save unnecessary calls to the engine.
	 * This, however, assumes that the mode isn't modified anywhere else.
	 */
	setCutsceneModeEnabled(enabled)
	{
		if (this.isCutsceneModeEnabled == enabled)
			return;

		Engine.Renderer_SetCutsceneModeEnabled(!!enabled);
		this.isCutsceneModeEnabled = enabled;
	}

	isInCutsceneMode()
	{
		return this.isCutsceneModeEnabled;
	}

	onSimulationUpdate()
	{
		if (GetSimState().cinemaPathPlaying)
		{
			if (this.parentObject.hidden)
				this.show();

			// Make sure to always be in cutscene mode while a cinema path is playing, no matter what the active
			// speech segment specifies, or if there even is one.
			this.setCutsceneModeEnabled(true);
		}
		else if (this.speechSegments.length)
		{
			// Necessary in the case that the cinema path has just ended.
			if (!this.speechSegments[this.activeSegmentIndex].enableCutsceneMode)
				this.setCutsceneModeEnabled(false);
		}
		else
			this.hide();
	}

	show()
	{
		if (!this.parentObject.hidden)
			return;

		this.primarySessionOverlays.hidden = true;
		this.bandbox.hidden = true;
		this.hotkeys.hidden = true;
		this.parentObject.hidden = false;

		if (GetSimState().cinemaPathPlaying)
			this.setCutsceneModeEnabled(true);
	}

	hide()
	{
		if (this.parentObject.hidden)
			return;

		this.primarySessionOverlays.hidden = !g_ShowGUI;
		this.hotkeys.hidden = false;
		this.parentObject.hidden = true;

		this.resetSpeechData();
		this.setCutsceneModeEnabled(false);
	}

	recalculateBarSizes()
	{
		const minHeight = 115;
		const width = this.parentObject.getComputedSize().right;
		const height = this.parentObject.getComputedSize().bottom;
		// The aspect ratio of 2.39:1 is typical in films and has a cinematic feel to it.
		const barHeight = Math.max(minHeight, (height - width / 2.39) / 2);
		this.barTop.size.bottom = barHeight;
		this.barBottom.size.top = -barHeight;
	}

	addSpeechSegmentToQueue(segment)
	{
		this.speechSegments.push(segment);
		if (this.parentObject.hidden)
			this.show();
		if (this.activeLineIndex == -1)
			this.loadNextLineOrClose();
	}

	loadNextLineOrClose()
	{
		if (this.loadNextLineTimer !== undefined)
		{
			clearTimeout(this.loadNextLineTimer);
			this.loadNextLineTimer = undefined;
		}

		const isVeryFirst = this.activeSegmentIndex === -1;
		const lastSpeakingUnit = !isVeryFirst && this.speechSegments[this.activeSegmentIndex]?.speakingUnit;
		if (isVeryFirst || this.activeLineIndex >= this.speechSegments[this.activeSegmentIndex].lines.length - 1)
		{
			// Load in the next segment.

			if (this.activeSegmentIndex >= this.speechSegments.length - 1)
			{
				// There is no more segment to load. Close.
				this.closeActiveSpeechSequence();
				return;
			}

			this.activeSegmentIndex++;
			this.activeLineIndex = 0;
		}
		else
			this.activeLineIndex++;

		this.loadActiveLine(lastSpeakingUnit);
	}

	loadActiveLine(lastSpeakingUnit)
	{
		const activeSegment = this.speechSegments[this.activeSegmentIndex];

		if (activeSegment.speakingUnit !== lastSpeakingUnit)
			// activeSegment.speakingUnit might be undefined, in which case this hides the speakingUnitDisplay.
			this.setSpeakingUnit(activeSegment.speakingUnit);
		if (activeSegment.speakingUnit && activeSegment.moveCameraToSpeakingUnit && !GetSimState().cinemaPathPlaying)
			Engine.CameraFollow(activeSegment.speakingUnit);


		this.displayText(activeSegment.lines[this.activeLineIndex]);

		if (!activeSegment.lineDurations?.length)
			throw new Error("speechSegment has no lineDurations");

		// Multiply the duration by the sim rate because lines might be synced with cinema paths, whose duration depend on the
		// sim rate as well. This assumes the sim rates doesn't change, but that should be fine.
		this.loadNextLineTimer = setTimeout(
			this.loadNextLineOrClose.bind(this), activeSegment.lineDurations[this.activeLineIndex] / Engine.GetSimRate()
		);

		this.setSpeechDisplayHidden(false);

		if (!GetSimState().cinemaPathPlaying)
			this.setCutsceneModeEnabled(activeSegment.enableCutsceneMode);
	}

	setSpeakingUnit(entityID)
	{
		if (!entityID)
		{
			this.speakingUnitDisplay.hidden = true;
			this.textContainer.size.left = -this.textContainer.size.right;
			return;
		}

		// TODO: Simplify this after #8761
		const templateData = GetTemplateData(GetEntityState(entityID).template);

		this.unitIcon.sprite = "stretched:session/portraits/" + templateData.icon;
		const primaryName = g_SpecificNamesPrimary ? templateData.name.specific : templateData.name.generic;
		const secondaryName = g_SpecificNamesPrimary ? templateData.name.generic : templateData.name.specific;
		this.unitName.caption = g_ShowSecondaryNames && primaryName !== secondaryName ?
			// Translation: Note: secondaryName is intended to always be placed in the line below.
			setStringTags(sprintf(translate("%(primaryName)s\n(%(secondaryName)s)"), {
				"primaryName": setStringTags(primaryName, this.UnitNameStringTags.Primary),
				"secondaryName": secondaryName
				// Apply the secondary name's string tags to everything so that the parenthesis are also affected.
			}), this.UnitNameStringTags.Secondary) :
			this.unitName.caption = sprintf(translate("%(primaryName)s"), {
				"primaryName": setStringTags(primaryName, this.UnitNameStringTags.Primary)
			});

		// Make more horizontal space for the primary and secondary name, if necessary.
		// They can be longer than the portrait is wide.
		this.speakingUnitDisplay.size.right = this.speakingUnitDisplay.size.left + Math.max(
			this.unitIcon.parent.size.right - this.unitIcon.parent.size.left,
			this.unitName.getPreferredTextSize().width
		);

		this.speakingUnitDisplay.hidden = false;
		this.textContainer.size.left = this.speakingUnitDisplay.size.right - this.textContainer.size.right;
	}

	displayText(text)
	{
		if (this.speakingUnitDisplay.hidden)
		{
			this.text.caption = translate(text);
			this.text.text_align = "center";
		}
		else
		{
			// Translation: Quotation marks around spoken text.
			this.text.caption = sprintf(translate("“%(text)s”"), {
				"text": translate(text)
			});
			this.text.text_align = "left";
		}
	}

	closeActiveSpeechSequence()
	{
		if (GetSimState().cinemaPathPlaying)
			this.setSpeechDisplayHidden(true); // Hide everything but the two black bars.
		else
			this.hide(); // Hide completely.
	}

	/**
	 * Hide or show the GUI objects used to display speech: everything except the two black bars.
	 */
	setSpeechDisplayHidden(hidden)
	{
		this.speechDisplay.hidden = hidden;

		if (hidden)
			this.resetSpeechData();
	}

	/**
	 * Enable or disable force-hiding the speech overlay (@see CinemaOverlay.setSpeechDisplayHidden ),
	 * but remember it's current state.
	 * (Achieved by actually only modifying it a parent object instead)
	 */
	overrideSpeechDisplayHidden(hidden)
	{
		this.speechDisplayParent.hidden = hidden;
	}

	resetSpeechData()
	{
		this.speechSegments = [];
		this.activeSegmentIndex = -1;
		this.activeLineIndex = -1;
	}

	getHotloadData()
	{
		if (!this.speechSegments.length)
			return {};

		return {
			"speechSegments": this.speechSegments,
			"activeSegmentIndex": this.activeSegmentIndex,
			"activeLineIndex": this.activeLineIndex
		};
	}
}

CinemaOverlay.prototype.UnitNameStringTags = {
	"Primary": { "font": "sans-bold-20", "color": "gold" },
	"Secondary": { "font": "sans-bold-18", "color": "white" }
};
