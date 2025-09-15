ChatMessageEvents.ClientModeration = class
{
	constructor(setupWindow, chatMessagesPanel)
	{
		this.chatMessagesPanel = chatMessagesPanel;

		this.messageArgs = {};

		setupWindow.controls.netMessages.registerNetMessageHandler("moderation", this.onClientModeration.bind(this));
		setupWindow.controls.netMessages.registerNetMessageHandler("moderation-listing", this.onClientModerationListing.bind(this));
	}

	onClientModeration(message)
	{
		this.messageArgs.code = message.code;
		this.messageArgs.targetPlayer = message.targetPlayer;
		this.messageArgs.originPlayer = message.originPlayer;

		this.chatMessagesPanel.addStatusMessage(sprintf(
			formatModerationResponse(message.code),
			this.messageArgs));
	}

	onClientModerationListing(message)
	{
		this.chatMessagesPanel.addStatusMessage(formatModerationListing(message.json, false));
	}
};
