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

#include "ICmpWaterManager.h"

#include "graphics/FramePacket.h"
#include "graphics/Terrain.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Message.h"
#include "simulation2/system/SimContext.h"

#include <string>

class CCmpWaterManager final : public ICmpWaterManager
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		// No need to subscribe to WaterChanged since we're actually the one sending those.
		componentManager.SubscribeToMessageType(MT_TerrainChanged);
	}

	DEFAULT_COMPONENT_ALLOCATOR(WaterManager)

	// Dynamic state:

	entity_pos_t m_WaterHeight;

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
	}

	void Deinit() override
	{
		// Clear the map size & data.
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->waterMapSize = 0;
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.NumberFixed_Unbounded("height", m_WaterHeight);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);

		deserialize.NumberFixed_Unbounded("height", m_WaterHeight);

		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->waterMapSize = GetSimContext().GetTerrain().GetVerticesPerSide();

		RecomputeWaterData();
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
			case MT_TerrainChanged:
			{
				// Tell the renderer to redraw part of the map.
				if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
				{
					const CMessageTerrainChanged& msgData = static_cast<const CMessageTerrainChanged&> (msg);
					GetSimContext().GetTerrain().UpdateHeightMipmap(msgData.i0, msgData.j0, msgData.i1, msgData.j1);
					packet->dirtyTerrain.push_back({msgData.i0, msgData.j0, msgData.i1, msgData.j1});
				}
				break;
			}
		}
	}

	void RecomputeWaterData() override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
		{
			packet->waterLevel = m_WaterHeight.ToFloat();
			packet->waterChanged = true;
		}
	}

	void SetWaterLevel(entity_pos_t h) override
	{
		if (m_WaterHeight == h)
			return;

		m_WaterHeight = h;

		RecomputeWaterData();

		CMessageWaterChanged msg;
		GetSimContext().GetComponentManager().BroadcastMessage(msg);
	}

	entity_pos_t GetWaterLevel(entity_pos_t /*x*/, entity_pos_t /*z*/) const override
	{
		return m_WaterHeight;
	}

	float GetExactWaterLevel(float /*x*/, float /*z*/) const override
	{
		return m_WaterHeight.ToFloat();
	}
};

REGISTER_COMPONENT_TYPE(WaterManager)
