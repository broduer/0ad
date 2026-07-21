class ResignConfirmation extends SessionMessageBox
{
	constructor()
	{
		super();

		const recipients = this.getEligibleRecipients();
		const canTributeAndResign = recipients.length > 0;

		this.Caption = translate("Are you sure you want to resign?");

		this.Buttons = [
			{
				"caption": translate("No")
			},
			{
				"caption": translate("Resign"),
				"onPress": () => Engine.PostNetworkCommand({
					"type": "resign"
				})
			}
		];

		if (canTributeAndResign)
			this.Buttons.push({
				"caption": translate("Tribute & Resign"),
				"tooltip": translate("Tribute resources to allies before resigning."),
				"onPress": () => this.tributeAndResign(recipients)
			});
	}

	getEligibleRecipients()
	{
		const playerID = Engine.GetPlayerID();
		if (playerID <= 0)
			return [];

		const simState = GetSimState();
		const playerState = simState.players[playerID];
		if (!playerState || playerState.state != "active" || playerState.team < 0)
			return [];

		return simState.players
			.map((otherPlayerState, otherPlayerID) => ({ "otherPlayerID": otherPlayerID, "otherPlayerState": otherPlayerState }))
			.filter(({ otherPlayerID, otherPlayerState }) =>
				otherPlayerID > 0 &&
				otherPlayerID != playerID &&
				otherPlayerState &&
				otherPlayerState.state == "active" &&
				otherPlayerState.team == playerState.team)
			.sort((a, b) =>
			{
				const totalDiff = this.getTotalTributableResources(a.otherPlayerState) -
					this.getTotalTributableResources(b.otherPlayerState);
				return totalDiff || a.otherPlayerID - b.otherPlayerID;
			});
	}

	getTotalTributableResources(playerState)
	{
		return g_ResourceData.GetTributableCodes().reduce(
			(total, resourceCode) => total + (playerState.resourceCounts[resourceCode] || 0),
			0);
	}

	tributeAndResign(recipients)
	{
		const tributableCodes = g_ResourceData.GetTributableCodes();
		const localPlayerState = GetSimState().players[Engine.GetPlayerID()];
		if (!localPlayerState)
		{
			Engine.PostNetworkCommand({ "type": "resign" });
			return;
		}

		for (const resourceCode of tributableCodes)
		{
			const roundedAmount = Math.floor((localPlayerState.resourceCounts[resourceCode] || 0) / 100) * 100;
			if (!roundedAmount)
				continue;

			const equalShare = Math.floor(roundedAmount / recipients.length / 100) * 100;
			const remainder = roundedAmount - equalShare * recipients.length;

			for (const recipient of recipients)
				if (equalShare > 0)
					Engine.PostNetworkCommand({
						"type": "tribute",
						"player": recipient.otherPlayerID,
						"amounts": {
							[resourceCode]: equalShare
						}
					});

			if (remainder > 0)
				Engine.PostNetworkCommand({
					"type": "tribute",
					"player": recipients[0].otherPlayerID,
					"amounts": {
						[resourceCode]: remainder
					}
				});
		}

		Engine.PostNetworkCommand({
			"type": "resign"
		});
	}
}

ResignConfirmation.prototype.Title = translate("Confirmation");
ResignConfirmation.prototype.Width = 600;
ResignConfirmation.prototype.Height = 200;
