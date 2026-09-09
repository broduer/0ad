/**
 * This class manages and displays the narrative pages received from trigger scripts.
 */
class NarrativeOverlay
{
	parentObject = Engine.GetGUIObjectByName("narrativeOverlay");
	// The objects we have to be careful not to overlap.
	borderingObjects = {
		"top": Engine.GetGUIObjectByName("civIcon"),
		"bottom": Engine.GetGUIObjectByName("selectionDetails")
	};
	overlayVariants = {};
	activeOverlay;

	pages = [];
	pageAddedHandlers = new Set();
	activePageIndex = -1;

	pauseControl;
	shouldPause = false;

	constructor(pauseControl, hotloadData)
	{
		this.pauseControl = pauseControl;

		for (const typeName in this.PAGE_TYPE)
		{
			const guiObjects = {};
			const parentObjectName = typeName.toLowerCase() + "NarrativeOverlay";
			const getGUIObject = (name) => Engine.GetGUIObjectByName(parentObjectName + name);
			guiObjects.parentObject = Engine.GetGUIObjectByName(parentObjectName);
			const parchment = getGUIObject("Parchment");
			const parchmentSize = parchment.getComputedSize();
			guiObjects.parchmentHeight = parchmentSize.bottom - parchmentSize.top;
			guiObjects.title = getGUIObject("Title");
			guiObjects.image = getGUIObject("Image");
			guiObjects.text = getGUIObject("Text");
			guiObjects.textContainer = getGUIObject("TextContainer");
			guiObjects.ornaments = getGUIObject("Ornaments");
			guiObjects.previousButton = getGUIObject("PreviousButton");
			guiObjects.nextButton = getGUIObject("NextButton");

			// TODO: Make GUI sizes copyable the ordinary way.
			guiObjects.textSize = {};
			for (const prop of ["left", "top", "right", "bottom", "rleft", "rtop", "rright", "rbottom"])
				guiObjects.textSize[prop] = guiObjects.text.size[prop];
			guiObjects.parentObject.hidden = true;
			guiObjects.previousButton.onPress = this.loadPreviousPage.bind(this);
			guiObjects.previousButton.caption = this.PreviousButtonCaption;
			guiObjects.previousButton.tooltip = this.PreviousButtonTooltip;
			guiObjects.nextButton.onPress = this.loadNextPageOrClose.bind(this);

			// setActiveOverlayScale in displayActivePage requires these to be relative.
			makeSizeRelative(parchment.parent);
			makeSizeRelative(parchment);
			makeSizeRelative(getGUIObject("Layout"));
			makeSizeRelative(getGUIObject("Buttons"));

			this.overlayVariants[this.PAGE_TYPE[typeName]] = guiObjects;
		}

		this.parentObject.onWindowResized = () => { if (!this.parentObject.hidden) this.displayActivePage(); };
		registerNetworkStatusChangeHandler(this.tryToPause.bind(this));

		this.parentObject.hidden = !hotloadData;
		if (hotloadData)
		{
			this.pages = hotloadData.pages;
			this.activePageIndex = hotloadData.activePageIndex;
			this.displayActivePage();
		}
	}

	open()
	{
		closeOpenDialogs();
		this.shouldPause = true;
		this.tryToPause();
		this.parentObject.hidden = false;
	}

	close()
	{
		this.pauseControl.setPaused(false, true);
		this.activePageIndex = -1;
		this.parentObject.hidden = true;
	}

	/**
	 * In multiplayer the game can only be paused once g_IsNetworkedActive is true, so not always immediately.
	 */
	tryToPause()
	{
		if (!this.shouldPause || (g_IsNetworked && !g_IsNetworkedActive))
			return;
		// Pause the game even in multiplayer; players should be able to take their time to read the narrative.
		this.pauseControl.setPaused(true, true);
		this.shouldPause = false;
	}

	registerPageAddedHandler(handler)
	{
		this.pageAddedHandlers.add(handler);
	}

	addPage(page, isNew)
	{
		this.pages.push(page);
		// If isNew is false, the page has been displayed already and we're just loading a saved game or rejoining.
		if (isNew && this.parentObject.hidden)
			this.loadFirstPage();
		else
			this.updateButtons();

		for (const handler of this.pageAddedHandlers)
			handler();
	}

	hasPagesToShow()
	{
		return this.pages.length > 0;
	}

	loadFirstPage()
	{
		if (this.parentObject.hidden && this.pages.length)
		{
			this.open();
			this.activePageIndex = 0;
			this.displayActivePage();
		}
	}

	loadNextPageOrClose()
	{
		if (this.activePageIndex < this.pages.length - 1)
		{
			this.activePageIndex++;
			this.displayActivePage();
		}
		else
			this.close();
	}

	loadPreviousPage()
	{
		this.activePageIndex--;
		this.displayActivePage();
	}

	displayActivePage()
	{
		const page = this.pages[this.activePageIndex];

		// On small screen resolutions there isn't enough space to display the large and sometimes even medium pages in
		// their full size. In those cases we have to scale down everything by some factor (including the potentially custom layout).
		const pageMargin = 25;
		const availableHeight =
			this.borderingObjects.bottom.getComputedSize().top - this.borderingObjects.top.getComputedSize().bottom - 2 * pageMargin;
		const setActiveOverlayScale = (factor) =>
		{
			const percent = (factor / 2) * 100;
			// Scale down the parent object only and assume its children have purely relative sizes.
			this.activeOverlay.parentObject.size = {
				"rleft": 50 - percent,
				"rtop": 50 - percent,
				"rright": 50 + percent,
				"rbottom": 50 + percent
			};
		};

		if (this.activeOverlay)
		{
			setActiveOverlayScale(1);
			this.activeOverlay.parentObject.hidden = true;
		}
		this.activeOverlay = this.overlayVariants[page.type];
		if (!this.activeOverlay)
		{
			error("NarrativeOverlay: Can't display page with unkown page type '" + page.type + "'.");
			return;
		}
		this.activeOverlay.parentObject.hidden = false;

		// Titles and images are optional, but all pages need a text.
		this.activeOverlay.title.caption = page.title ? translate(page.title) : "";
		this.activeOverlay.text.caption = translate(page.text);
		this.activeOverlay.text.text_align = page.textAlign || "center";
		this.activeOverlay.image.hidden = page.image === undefined;
		if (!this.activeOverlay.image.hidden)
			this.activeOverlay.image.sprite = "stretched:narratives/" + page.image;

		const defaultTitleHeight = page.type == this.PAGE_TYPE.SMALL ? 35 : page.type == this.PAGE_TYPE.MEDIUM ? 47 : 63;
		const defaultVerticalTextMargin = page.type == this.PAGE_TYPE.SMALL ? 7 : page.type == this.PAGE_TYPE.MEDIUM ? 10 : 15;
		this.activeOverlay.title.size = page.titleSize || {
			"rleft": 0, "left": 0,
			"rright": 100, "right": 0,
			"rtop": 0, "top": 0,
			// Even if there's no title, leave some additional free space above the text, that looks more natural.
			"rbottom": 0, "bottom": (page.title ? defaultTitleHeight : defaultTitleHeight / 2.5)
		};
		this.activeOverlay.textContainer.size = page.textSize || {
			"rleft": 0, "left": 0,
			"rright": 100, "right": 0,
			"rtop": 0, "top": this.activeOverlay.title.size.bottom + defaultVerticalTextMargin,
			"rbottom": 100, "bottom": -defaultVerticalTextMargin
		};

		this.activeOverlay.text.buffer_zone = page.type == this.PAGE_TYPE.SMALL ? 12 : page.type == this.PAGE_TYPE.MEDIUM ? 14 : 17;
		this.activeOverlay.text.size = this.activeOverlay.textSize;

		// There are no restrictions on text/caption length, it can vary a lot, also between languages.
		// If it's short and doesn't occupy the entire container, scale the container down.
		this.scaleContainerToText();

		const minOrnamentsWidth = 320;
		const ornamentsWidth = Math.max(this.activeOverlay.text.getTextSize().width - 100, minOrnamentsWidth);
		this.activeOverlay.ornaments.size = {
			"rleft": 50, "left": -ornamentsWidth / 2,
			"rright": 50, "right": ornamentsWidth / 2,
			"rbottom": 100
		};

		// setActiveOverlayScale requires is children's sizes to only have relative values.
		makeSizeRelative(this.activeOverlay.title);
		makeSizeRelative(this.activeOverlay.textContainer);
		makeSizeRelative(this.activeOverlay.ornaments);
		makeSizeRelative(this.activeOverlay.text);

		setActiveOverlayScale(Math.min(1, availableHeight / this.activeOverlay.parchmentHeight));
		this.updateButtons();
	}

	/**
	 * Simple helper method that scales down the text container (a reasonable amount or) until it's filled completely by the drawn
	 * text/caption but without a scrollbar, while mainining its original aspect ratio as well as possible.
	 * This might seem like overkill, but looks a lot better than just scaling the container down vertically to the text height.
	 */
	scaleContainerToText()
	{
		const textContainerSize = this.activeOverlay.textContainer.getComputedSize();
		let currentWidth = textContainerSize.right - textContainerSize.left;
		const originalHeight = textContainerSize.bottom - textContainerSize.top;
		// Calculate the (minimal) height the text container needs to show the text without a scrollbar.
		// This equals the drawn text's height plus the ornaments' heights.
		const getDisplayHeight = () =>
			this.activeOverlay.text.getTextSize().height + this.activeOverlay.text.size.top - this.activeOverlay.text.size.bottom;
		let currentDisplayHeight = getDisplayHeight();
		const desiredAspectRatio = currentWidth / originalHeight;

		// We iteratively reduce the width until currentWidth / currentDisplayHeight is as close to desiredAspectRatio as possible.
		const numIterations = 10;
		const widthDecrement = (currentWidth * 0.3) / numIterations; // Never make it narrower than 70% of the original width.
		const halfWidthDecrement = widthDecrement / 2;
		let previousAspectRatio = currentWidth / currentDisplayHeight;
		for (let i = 0; i < numIterations; i++)
		{
			this.activeOverlay.textContainer.size.left += halfWidthDecrement;
			this.activeOverlay.textContainer.size.right -= halfWidthDecrement;
			currentWidth -= widthDecrement;
			currentDisplayHeight = getDisplayHeight();
			const currentAspectRatio = currentWidth / currentDisplayHeight;
			if (currentDisplayHeight > originalHeight || // Never make it higher than originally.
				Math.abs(currentAspectRatio - desiredAspectRatio) > Math.abs(previousAspectRatio - desiredAspectRatio))
			{
				// During the previous iteration we achieved a better aspect ratio, so revert to that and stop.
				this.activeOverlay.textContainer.size.left -= halfWidthDecrement;
				this.activeOverlay.textContainer.size.right += halfWidthDecrement;

				const halfHeightDifference = Math.max(0, (originalHeight - getDisplayHeight()) / 2);
				this.activeOverlay.textContainer.size.top += halfHeightDifference;
				this.activeOverlay.textContainer.size.bottom -= halfHeightDifference;
				return;
			}
			previousAspectRatio = currentAspectRatio;
		}
	}

	updateButtons()
	{
		if (!this.activeOverlay)
			return;

		const hasNextPageToShow = this.activePageIndex < this.pages.length - 1;
		this.activeOverlay.nextButton.caption = hasNextPageToShow ? this.NextButtonCaption : this.CloseButtonCaption;
		this.activeOverlay.nextButton.tooltip = hasNextPageToShow ? this.NextButtonTooltip : this.CloseButtonTooltip;
		this.activeOverlay.previousButton.enabled = this.activePageIndex > 0;
		this.activeOverlay.previousButton.hidden = this.pages.length === 1;
	}

	getHotloadData()
	{
		return {
			"pages": this.pages,
			"activePageIndex": this.activePageIndex
		};
	}
}

NarrativeOverlay.prototype.CloseButtonCaption = translateWithContext("button caption", "Close");
NarrativeOverlay.prototype.CloseButtonTooltip = translateWithContext("button tooltip", "Close this page.");
NarrativeOverlay.prototype.NextButtonCaption = translateWithContext("button caption", "Next");
NarrativeOverlay.prototype.NextButtonTooltip = translateWithContext("button tooltip", "Switch to the next page.");
NarrativeOverlay.prototype.PreviousButtonCaption = translateWithContext("button caption", "Previous");
NarrativeOverlay.prototype.PreviousButtonTooltip = translateWithContext("button tooltip", "Switch to the previos page.");

/**
 * The different types (i.e. sizes) of narrative pages.
 */
NarrativeOverlay.prototype.PAGE_TYPE = Object.freeze({
	"SMALL": 0,
	"MEDIUM": 1,
	"LARGE": 2
});

