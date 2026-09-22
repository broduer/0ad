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

#include "ICmpRangeOverlayRenderer.h"

#include "graphics/Color.h"
#include "graphics/Overlay.h"
#include "graphics/RenderWorld.h"
#include "ps/CStrIntern.h"
#include "renderer/Renderer.h"
#include "renderer/Scene.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpOwnership.h"
#include "simulation2/components/ICmpPlayer.h"
#include "simulation2/components/ICmpPlayerManager.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/helpers/Render.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Message.h"

#include <memory>
#include <string>
#include <vector>

class CCmpRangeOverlayRenderer final : public ICmpRangeOverlayRenderer
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_Deserialized);
		componentManager.SubscribeToMessageType(MT_OwnershipChanged);
		componentManager.SubscribeToMessageType(MT_PlayerColorChanged);
	}

	DEFAULT_COMPONENT_ALLOCATOR(RangeOverlayRenderer)

	CCmpRangeOverlayRenderer()
	{
		m_Color = CColor(0.f, 0.f, 0.f, 1.f);
	}

	~CCmpRangeOverlayRenderer() = default;

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
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
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer&) override
	{
		Init(paramNode);
	}

	void ResetRangeOverlays() override
	{
		m_Descriptors.clear();
		MarkRenderDirty();
	}

	void AddRangeOverlay(float radius, const std::string& texture, const std::string& textureMask, float thickness) override
	{
		if (!CRenderer::IsInitialised())
			return;

		SOverlayDescriptor rangeOverlayDescriptor;
		rangeOverlayDescriptor.m_Radius = radius;
		rangeOverlayDescriptor.m_LineTexture = CStrIntern(TEXTURE_BASE_PATH + texture);
		rangeOverlayDescriptor.m_LineTextureMask = CStrIntern(TEXTURE_BASE_PATH + textureMask);
		rangeOverlayDescriptor.m_LineThickness = thickness;

		m_Descriptors.push_back(rangeOverlayDescriptor);
		MarkRenderDirty();
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_FrameStep:
		{
			PublishRenderState();
			break;
		}
		case MT_Deserialized:
		case MT_OwnershipChanged:
		case MT_PlayerColorChanged:
		{
			UpdateColor();
			break;
		}
		}
	}

private:
	void UpdateColor() override
	{
		CmpPtr<ICmpOwnership> cmpOwnership(GetEntityHandle());
		if (!cmpOwnership)
			return;

		player_id_t owner = cmpOwnership->GetOwner();
		if (owner == INVALID_PLAYER)
			return;

		CmpPtr<ICmpPlayerManager> cmpPlayerManager(GetSystemEntity());
		if (!cmpPlayerManager)
			return;

		CmpPtr<ICmpPlayer> cmpPlayer(GetSimContext(), cmpPlayerManager->GetPlayerByID(owner));
		if (!cmpPlayer)
			return;

		CColor color = cmpPlayer->GetDisplayedColor();
		m_Color = color;
		MarkRenderDirty();
	}

	void MarkRenderDirty()
	{
		if (m_RenderDirty)
			return;
		m_RenderDirty = true;
		GetSimContext().GetComponentManager().RequestFrameStep(this);
	}

	/**
	 * Record the descriptors and the colour for the render world.
	 */
	void PublishRenderState()
	{
		m_RenderDirty = false;
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->overlays.emplace_back(RenderWorld::Overlay::SetRanges{GetEntityId(), m_Descriptors, m_Color});
	}

	const char* TEXTURE_BASE_PATH = "art/textures/selection/";
	// Recorded for the render world at the frame step.
	std::vector<SOverlayDescriptor> m_Descriptors;
	CColor m_Color;
	bool m_RenderDirty = false;
};

REGISTER_COMPONENT_TYPE(RangeOverlayRenderer)
