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

#include "RenderProjectiles.h"

#include "graphics/GroundView.h"
#include "graphics/ModelAbstract.h"
#include "graphics/Unit.h"
#include "graphics/UnitManager.h"
#include "maths/Frustum.h"
#include "maths/Matrix3D.h"
#include "maths/Quaternion.h"
#include "ps/Profile.h"
#include "renderer/Scene.h"
#include "simulation2/helpers/Los.h"

#include <cmath>
#include <utility>

namespace RenderWorld
{

namespace
{

// Time (in seconds) before projectiles that stuck in the ground are destroyed.
constexpr float PROJECTILE_DECAY_TIME = 30.0f;

} // anonymous namespace

bool SLosGrid::IsVisible(const CVector3D& position) const
{
	if (revealAll)
		return true;
	const std::ptrdiff_t i{static_cast<std::ptrdiff_t>(0.5f + position.X / LOS_TILE_SIZE)};
	const std::ptrdiff_t j{static_cast<std::ptrdiff_t>(0.5f + position.Z / LOS_TILE_SIZE)};
	const std::ptrdiff_t size{static_cast<std::ptrdiff_t>(verticesPerSide)};
	if (i < 0 || j < 0 || i >= size || j >= size)
		return false;
	return visible[j * size + i] != 0;
}

CVector3D CProjectiles::SProjectile::Position(const float t) const
{
	float t2 = t;
	if (t2 > timeHit)
		t2 = timeHit + std::log(1.0f + t2 - timeHit);

	CVector3D ret(origin);
	ret.X += v.X * t2;
	ret.Z += v.Z * t2;
	ret.Y += v.Y * t2 - 0.5f * gravity * t * t;
	return ret;
}

CProjectiles::CProjectiles(CUnitManager& unitManager, const IGroundView& ground)
	: m_UnitManager{unitManager}, m_Ground{ground}
{
}

CProjectiles::~CProjectiles()
{
	for (SProjectile& projectile : m_Projectiles)
		m_UnitManager.DeleteUnit(projectile.unit);
	for (SImpact& impact : m_Impacts)
		m_UnitManager.DeleteUnit(impact.unit);
}

void CProjectiles::Apply(const ProjectileRecord& record)
{
	if (const Projectile::Launch* launch = std::get_if<Projectile::Launch>(&record))
		Launch(*launch);
	else
		Remove(std::get<Projectile::Remove>(record).id);
}

void CProjectiles::Launch(const Projectile::Launch& launch)
{
	SProjectile projectile;
	projectile.unit = m_UnitManager.CreateUnit(launch.actorName, INVALID_ENTITY, launch.seed);
	// The error will have already been logged.
	if (!projectile.unit)
		return;
	projectile.id = launch.id;
	projectile.origin = launch.origin;
	projectile.pos = launch.origin;
	projectile.time = 0.0f;
	projectile.gravity = launch.gravity;
	projectile.impactActorName = launch.impactActorName;
	projectile.impactAnimationLifetime = launch.impactActorName.empty() ? 0.0f : launch.impactAnimationLifetime;
	projectile.stopped = false;
	projectile.impactCreated = false;

	CVector3D offset{launch.target};
	offset -= projectile.pos;
	const float horizDistance{std::sqrt(offset.X * offset.X + offset.Z * offset.Z)};
	projectile.timeHit = horizDistance / launch.speed;
	projectile.v = offset * (1.0f / projectile.timeHit);
	projectile.v.Y = offset.Y / projectile.timeHit + 0.5f * projectile.gravity * projectile.timeHit;

	m_Projectiles.push_back(std::move(projectile));
}

void CProjectiles::Remove(const std::uint32_t id)
{
	for (size_t i = 0; i < m_Projectiles.size(); ++i)
	{
		if (m_Projectiles[i].id != id)
			continue;
		// Delete in-place by swapping with the last in the list.
		std::swap(m_Projectiles[i], m_Projectiles.back());
		m_UnitManager.DeleteUnit(m_Projectiles.back().unit);
		m_Projectiles.pop_back();
		return;
	}
}

void CProjectiles::Advance(SProjectile& projectile, const float dt) const
{
	projectile.time += dt;
	if (projectile.stopped)
		return;

	CVector3D delta;
	if (dt < 0.1f)
		delta = projectile.pos;
	else // For big dt delta is unprecise
		delta = projectile.Position(projectile.time - 0.1f);

	projectile.pos = projectile.Position(projectile.time);

	delta = projectile.pos - delta;

	// If we've passed the target position and haven't stopped yet,
	// carry on until we reach solid land.
	if (projectile.time >= projectile.timeHit)
	{
		const float h{m_Ground.GetExactGroundLevel(projectile.pos.X, projectile.pos.Z)};
		if (projectile.pos.Y < h)
		{
			projectile.pos.Y = h; // stick precisely to the terrain
			projectile.stopped = true;
		}
	}

	// Construct a rotation matrix so that (0,1,0) is in the direction of 'delta'.
	const CVector3D up(0, 1, 0);

	delta.Normalize();
	CVector3D axis{up.Cross(delta)};
	if (axis.LengthSquared() < 0.0001f)
		axis = CVector3D(1, 0, 0); // if up & delta are almost collinear, rotate around some other arbitrary axis
	else
		axis.Normalize();

	const float angle{std::acos(up.Dot(delta))};

	CMatrix3D transform;
	CQuaternion quat;
	quat.FromAxisAngle(axis, angle);
	quat.ToMatrix(transform);
	transform.Translate(projectile.pos);

	projectile.unit->GetModel().SetTransform(transform);
}

void CProjectiles::Interpolate(const float frameTime)
{
	PROFILE3("render projectiles interpolate");

	for (SProjectile& projectile : m_Projectiles)
		Advance(projectile, frameTime);

	// Remove the ones that have reached their target.
	for (size_t i = 0; i < m_Projectiles.size();)
	{
		SProjectile& projectile = m_Projectiles[i];
		if (!projectile.stopped)
		{
			++i;
			continue;
		}

		if (!projectile.impactActorName.empty() && !projectile.impactCreated)
		{
			projectile.impactCreated = true;
			CMatrix3D transform;
			CQuaternion quat;
			quat.ToMatrix(transform);
			transform.Translate(projectile.pos);
			if (CUnit* unit = m_UnitManager.CreateUnit(projectile.impactActorName, INVALID_ENTITY, m_ImpactSeed++))
			{
				unit->GetModel().SetTransform(transform);
				m_Impacts.push_back({unit, projectile.pos, projectile.impactAnimationLifetime});
			}
		}

		// Projectiles hitting targets get removed immediately.
		// Those hitting the ground stay for a while, because it looks pretty.
		if (projectile.time - projectile.timeHit > PROJECTILE_DECAY_TIME)
		{
			std::swap(m_Projectiles[i], m_Projectiles.back());
			m_UnitManager.DeleteUnit(m_Projectiles.back().unit);
			m_Projectiles.pop_back();
			continue;
		}
		++i;
	}

	for (size_t i = 0; i < m_Impacts.size();)
	{
		if (m_Impacts[i].time > 0)
		{
			m_Impacts[i].time -= frameTime;
			++i;
			continue;
		}
		std::swap(m_Impacts[i], m_Impacts.back());
		m_UnitManager.DeleteUnit(m_Impacts.back().unit);
		m_Impacts.pop_back();
	}
}

bool CProjectiles::IsVisible(const CVector3D& position) const
{
	return m_Los && m_Los->IsVisible(position);
}

void CProjectiles::Submit(SceneCollector& collector, const CFrustum& frustum, const bool culling)
{
	const auto submit = [&](CModelAbstract& model, const CVector3D& position) {
		// Don't display objects outside the visible area.
		if (!IsVisible(position))
			return;
		model.ValidatePosition();
		if (culling && !frustum.IsBoxVisible(model.GetWorldBoundsRec()))
			return;
		collector.SubmitRecursive(&model);
	};
	for (SProjectile& projectile : m_Projectiles)
		submit(projectile.unit->GetModel(), projectile.pos);
	for (SImpact& impact : m_Impacts)
		submit(impact.unit->GetModel(), impact.pos);
}

} // namespace RenderWorld
