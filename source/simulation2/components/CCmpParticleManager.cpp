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

#include "ICmpParticleManager.h"

#include "graphics/FramePacket.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/SimContext.h"

#include <string>

/**
 * Chooses the clock the render world's particles run on.
 */
class CCmpParticleManager final : public ICmpParticleManager
{
public:
	static void ClassInit(CComponentManager&)
	{
	}

	DEFAULT_COMPONENT_ALLOCATOR(ParticleManager)

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer&) override
	{
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer&) override
	{
		Init(paramNode);
	}

	void HandleMessage(const CMessage&, bool /*global*/) override
	{
	}

	void SetUseSimTime(bool flag) override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->particlesUseSimTime = flag;
	}
};

REGISTER_COMPONENT_TYPE(ParticleManager)
