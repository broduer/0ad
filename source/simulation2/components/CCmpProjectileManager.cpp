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

#include "ICmpProjectileManager.h"

#include "graphics/FramePacket.h"
#include "maths/Fixed.h"
#include "maths/FixedVector3D.h"
#include "maths/Vector3D.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/SimContext.h"

#include <cstdint>
#include <string>

/**
 * Launches the visual projectiles, which the render world owns from launch to
 * removal. The simulation only numbers them, so the attack code can remove the
 * one that hit its target.
 */
class CCmpProjectileManager final : public ICmpProjectileManager
{
public:
	static void ClassInit(CComponentManager&)
	{
	}

	DEFAULT_COMPONENT_ALLOCATOR(ProjectileManager)

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
		m_ActorSeed = 0;
		m_NextId = 1;
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serialize) override
	{
		// Because this is just graphical effects, and because it's all non-deterministic floating point,
		// we don't do much serialization here.
		// (That means projectiles will vanish if you save/load - is that okay?)

		// The attack code stores the id so that the projectile gets deleted when it hits the target
		serialize.NumberU32_Unbounded("next id", m_NextId);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);

		// The attack code stores the id so that the projectile gets deleted when it hits the target
		deserialize.NumberU32_Unbounded("next id", m_NextId);
	}

	void HandleMessage(const CMessage&, bool /*global*/) override
	{
	}

	uint32_t LaunchProjectileAtPoint(const CFixedVector3D& launchPoint, const CFixedVector3D& target, fixed speed, fixed gravity, const std::wstring& actorName, const std::wstring& impactActorName, fixed impactAnimationLifetime) override
	{
		// This is network synced so don't use GUI checks before incrementing or it breaks any non GUI simulations
		const uint32_t id = m_NextId++;

		RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket();
		if (!packet || actorName.empty())
			return id;

		packet->projectiles.emplace_back(RenderWorld::Projectile::Launch{id, CVector3D(launchPoint),
			CVector3D(target), speed.ToFloat(), gravity.ToFloat(), actorName, impactActorName,
			impactAnimationLifetime.ToFloat(), m_ActorSeed++});
		return id;
	}

	void RemoveProjectile(uint32_t id) override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->projectiles.emplace_back(RenderWorld::Projectile::Remove{id});
	}

private:
	uint32_t m_ActorSeed;
	uint32_t m_NextId;
};

REGISTER_COMPONENT_TYPE(ProjectileManager)
