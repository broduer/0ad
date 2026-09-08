// It's good practice to give names to the different pages, not just indices.
const DEMO_PAGE = Object.freeze({
	"SMALL_1": 0,
	"SMALL_2": 1,
	"MEDIUM_1": 2,
	"MEDIUM_2": 3,
	"LARGE_1": 4,
	"LARGE_2": 5
});

Trigger.prototype.InitDemo = function()
{
	TriggerHelper.DisplayNarrativePage(this.DemoPages[DEMO_PAGE.SMALL_1]);
	TriggerHelper.DisplayNarrativePage(this.DemoPages[DEMO_PAGE.SMALL_2]);
	TriggerHelper.DisplayNarrativePage(this.DemoPages[DEMO_PAGE.MEDIUM_1]);
	TriggerHelper.DisplayNarrativePage(this.DemoPages[DEMO_PAGE.MEDIUM_2]);
	TriggerHelper.DisplayNarrativePage(this.DemoPages[DEMO_PAGE.LARGE_1]);
	TriggerHelper.DisplayNarrativePage(this.DemoPages[DEMO_PAGE.LARGE_2]);
};

/**
 * Some example pages to demonstrate the different effects and layouts possible.
 *
 * Note: For actual narratives it is recommended to (unlike done here) stick with single page sizes and layouts to
 * keep the look and feel consistent.
 */
Trigger.prototype.DemoPages = {
	// Just a simple, short text.
	[DEMO_PAGE.SMALL_1]: {
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.SMALL,
		// TODO: Translate these texts?
		"text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Phasellus iaculis justo quam, a elementum dui egestas in. Nulla scelerisque aliquam leo. Nam hendrerit sagittis dolor, consequat hendrerit lectus pulvinar ac."
	},
	// An even shorter text, but aligned to the left and with a title.
	[DEMO_PAGE.SMALL_2]: {
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.SMALL,
		"title": "LOREM IPSUM",
		"text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Phasellus iaculis justo quam, a elementum dui egestas in."
	},
	// Quite a long text with custom alignment and a title.
	[DEMO_PAGE.MEDIUM_1]: {
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.MEDIUM,
		"title": "LOREM IPSUM DOLOR SIT AMET",
		"text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Nunc vitae egestas erat. Praesent non lorem neque. Pellentesque posuere pretium nibh, auctor aliquet est fermentum ut. Nam ut augue sit amet tortor tincidunt placerat. Duis lacinia quam quis dapibus aliquet.\nDonec vulputate laoreet tristique. Vivamus porttitor, leo id convallis rhoncus, orci enim blandit turpis, sit amet maximus diam eros ut libero.\nPhasellus eleifend in massa a feugiat. Nunc blandit suscipit tincidunt. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Curabitur varius imperdiet purus nec ultricies. Praesent pulvinar orci lacus, id sollicitudin risus consequat sit amet. Proin viverra viverra massa, ac malesuada magna tempor vel.",
		"textAlign": "left"
	},
	// Text in the center with blood drops and spatters around it.
	// Notice how the image fully covers and replaces the parchment in the background; the effect wouldn't be possible with a simple overlay.
	[DEMO_PAGE.MEDIUM_2]: {
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.MEDIUM,
		"image": "demo/parchment_blood.png",
		"text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In rutrum urna id egestas rhoncus. Phasellus quis dignissim augue, quis pharetra dui. Mauris metus lacus, luctus eget enim eget, interdum gravida sapien.\nAenean facilisis porttitor maximus. Aliquam eget velit a dolor sodales hendrerit eu a leo. Interdum et malesuada fames ac ante ipsum primis in faucibus. Duis non dui orci. Integer arcu nunc, congue et augue at, imperdiet facilisis metus. Maecenas sollicitudin pretium aliquet.",
		"textSize": "100 40 100%-100 100%"
	},
	// Horizontal layout: charcoal drawing on the left and text on the right.
	[DEMO_PAGE.LARGE_1]: {
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.LARGE,
		"image": "demo/legionary.png",
		"title": "LOREM IPSUM",
		"titleSize": "50%-200 0 50%+200 35",
		"text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Donec mauris eros, blandit et urna sit amet, egestas scelerisque mauris. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Maecenas vestibulum nec magna eu consectetur.\n\nMorbi vulputate efficitur dignissim. Ut blandit magna sit amet nibh mattis laoreet. Nunc ultrices tristique libero, non cursus turpis accumsan eget. Integer vitae libero iaculis, malesuada magna quis, vestibulum ipsum. Nulla non congue odio. Maecenas in luctus ex.",
		"textSize": "490 65 100% 100%-20"
	},
	// Vertical layout: Framed image with a wide aspect ratio on the top and some text below.
	// Notice that the texts is too long to be displayed in the specified text field; that isn't necessary, a scrollbar is added automatically.
	[DEMO_PAGE.LARGE_2]: {
		"type": TriggerHelper.NARRATIVE_PAGE_TYPE.LARGE,
		"image": "demo/napata.png",
		"text": "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Pellentesque nisi sapien, aliquam sed ultrices ac, mattis ac turpis. Cras eu laoreet quam. Cras tincidunt bibendum felis, ac luctus ipsum placerat eu. Nullam pretium erat nisi, nec elementum urna maximus quis. Nulla sodales non ipsum quis cursus. Phasellus ac facilisis leo. Suspendisse potenti.\n\nNullam pellentesque, erat nec commodo pharetra, mauris libero vulputate ex, ac scelerisque augue justo non purus. Morbi feugiat lectus ex, in vulputate orci fringilla non. Suspendisse nulla justo, porttitor nec erat id, rhoncus scelerisque est. Integer vitae dui vel elit scelerisque porta a nec ipsum. Suspendisse vehicula lectus vel tortor mattis, sit amet condimentum libero aliquam. Donec porta arcu vel maximus luctus. Proin ullamcorper mollis pretium. Nunc ex est, consequat ut orci sed, faucibus interdum arcu.",
		"textSize": "10 320 100%-10 100%-15"
	}
};

Engine.QueryInterface(SYSTEM_ENTITY, IID_Trigger).RegisterTrigger("OnInitGame", "InitDemo", { "enabled": true });
