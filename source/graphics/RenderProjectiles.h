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

#ifndef INCLUDED_RENDERPROJECTILES
#define INCLUDED_RENDERPROJECTILES

#include "graphics/FramePacket.h"
#include "lib/code_annotation.h"
#include "maths/Vector3D.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CFrustum;
class CUnit;
class CUnitManager;
class IGroundView;
class SceneCollector;

namespace RenderWorld
{

/**
 * The projectiles in flight and their impact animations: purely visual, so
 * the render world owns them from launch to removal. The simulation only
 * launches them and removes the ones that hit their target.
 */
class CProjectiles
{
	NONCOPYABLE(CProjectiles);
public:
	CProjectiles(CUnitManager& unitManager, const IGroundView& ground);
	~CProjectiles();

	void Apply(const ProjectileRecord& record);
	void SetLos(std::shared_ptr<const SLosGrid> los) { m_Los = std::move(los); }

	void Interpolate(float frameTime);
	void Submit(SceneCollector& collector, const CFrustum& frustum, bool culling);

private:
	struct SProjectile
	{
		CUnit* unit;
		std::uint32_t id;
		CVector3D origin;
		CVector3D pos;
		CVector3D v;
		float time;
		float timeHit;
		float gravity;
		std::wstring impactActorName;
		float impactAnimationLifetime;
		bool stopped;
		bool impactCreated;

		CVector3D Position(float t) const;
	};

	struct SImpact
	{
		CUnit* unit;
		CVector3D pos;
		float time;
	};

	void Launch(const Projectile::Launch& launch);
	void Remove(std::uint32_t id);
	void Advance(SProjectile& projectile, float dt) const;
	bool IsVisible(const CVector3D& position) const;

	CUnitManager& m_UnitManager;
	const IGroundView& m_Ground;
	std::vector<SProjectile> m_Projectiles;
	std::vector<SImpact> m_Impacts;
	std::uint32_t m_ImpactSeed{0};
	std::shared_ptr<const SLosGrid> m_Los;
};

} // namespace RenderWorld

#endif // INCLUDED_RENDERPROJECTILES
