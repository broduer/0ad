/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "ICmpOverlayRenderer.h"

#include "graphics/Color.h"
#include "graphics/FramePacket.h"
#include "maths/FixedVector2D.h"
#include "maths/Vector3D.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Profile.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Message.h"
#include "simulation2/system/SimContext.h"

#include <cstddef>
#include <string>
#include <vector>

class CCmpOverlayRenderer final : public ICmpOverlayRenderer
{
public:
	static void ClassInit(CComponentManager &componentManager)
	{
		componentManager.SubscribeToMessageType(MT_VisibilityChanged);
	}

	DEFAULT_COMPONENT_ALLOCATOR(OverlayRenderer)

	// Recorded for the render world at the frame step.
	std::vector<RenderWorld::SSprite> m_Sprites;

	// NOT SAFE TO SERIALIZE - used to hide status bars/auras of entities invisible to the player.
	// We also can't just deserialize it because annoyingly the range manager computes LOS data on deserialize...
	LosVisibility m_VisibilityToCurrentPlayer = LosVisibility::VISIBLE;
	// So to work around that, just store which player we saved this for last.
	player_id_t m_LastStoredPlayerVisibility = INVALID_PLAYER;
	bool m_RenderDirty = false;
	// Whether subscribed to turn starts, to follow changes of the displayed player.
	bool m_FollowingTurns = false;

	static std::string GetSchema()
	{
		return "<empty/>";
	}

	void Init(const CParamNode&) override
	{
	}

	void Deinit() override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->overlays.emplace_back(RenderWorld::Overlay::RemoveEntity{GetEntityId()});
	}

	void Serialize(ISerializer&) override
	{
		// TODO: should we do anything here?
		// or should we expect other components to reinitialise us
		// after deserialization?
	}

	void Deserialize(const CParamNode &paramNode, IDeserializer&) override
	{
		Init(paramNode);
	}

	void HandleMessage(const CMessage &msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_FrameStep:
		{
			PublishRenderState();
			break;
		}
		case MT_TurnStart:
		{
			if (m_LastStoredPlayerVisibility != GetSimContext().GetCurrentDisplayedPlayer())
				MarkRenderDirty();
			break;
		}
		case MT_VisibilityChanged:
		{
			const CMessageVisibilityChanged &msgData = static_cast<const CMessageVisibilityChanged &>(msg);
			if (msgData.player == GetSimContext().GetCurrentDisplayedPlayer())
			{
				m_VisibilityToCurrentPlayer = static_cast<LosVisibility>(msgData.newVisibility);
				MarkRenderDirty();
			}
			break;
		}
		}
	}

	void MarkRenderDirty()
	{
		if (m_RenderDirty)
			return;
		m_RenderDirty = true;
		GetSimContext().GetComponentManager().RequestFrameStep(this);
	}

	void Reset() override
	{
		m_Sprites.clear();
		MarkRenderDirty();
	}

	void AddSprite(const VfsPath &textureName, const CFixedVector2D &corner0, const CFixedVector2D &corner1, const CFixedVector3D &position, const std::string &color) override
	{
		CColor colorObj(1.0f, 1.0f, 1.0f, 1.0f);
		if (!colorObj.ParseString(color, 1))
			LOGERROR("OverlayRenderer: Error parsing '%s'", color);

		m_Sprites.push_back({textureName, corner0.X.ToFloat(), corner0.Y.ToFloat(),
			corner1.X.ToFloat(), corner1.Y.ToFloat(), colorObj, CVector3D(position)});

		MarkRenderDirty();
	}

	/**
	 * Record the sprites and their visibility for the render world.
	 */
	void PublishRenderState()
	{
		m_RenderDirty = false;
		// Only visible sprites need their visibility kept current.
		if (m_FollowingTurns == m_Sprites.empty())
		{
			m_FollowingTurns = !m_Sprites.empty();
			GetSimContext().GetComponentManager().DynamicSubscriptionNonsync(MT_TurnStart, this, m_FollowingTurns);
		}
		RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket();
		if (!packet)
			return;
		// Recompute our visibility if needed.
		if (m_LastStoredPlayerVisibility != GetSimContext().GetCurrentDisplayedPlayer())
		{
			CmpPtr<ICmpRangeManager> cmpRangeManager(GetSystemEntity());
			m_VisibilityToCurrentPlayer = static_cast<LosVisibility>(cmpRangeManager->GetLosVisibility(GetEntityHandle(), GetSimContext().GetCurrentDisplayedPlayer()));
			m_LastStoredPlayerVisibility = GetSimContext().GetCurrentDisplayedPlayer();
		}
		packet->overlays.emplace_back(RenderWorld::Overlay::SetSprites{GetEntityId(), m_Sprites, m_VisibilityToCurrentPlayer});
	}
};

REGISTER_COMPONENT_TYPE(OverlayRenderer)
