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

#ifndef INCLUDED_PARTICLEMANAGER
#define INCLUDED_PARTICLEMANAGER

#include "graphics/ParticleEmitter.h"
#include "graphics/ParticleEmitterType.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/path.h"
#include "lib/status.h"

#include <unordered_map>
#include <vector>

class CFrustum;
class SceneCollector;

namespace Renderer::Backend { class IDevice; }

class CParticleManager
{
public:
	CParticleManager(Renderer::Backend::IDevice& device);
	~CParticleManager();

	CParticleEmitterType* LoadEmitterType(const VfsPath& path);

	/**
	 * Tell the manager to handle rendering of an emitter that is no longer
	 * attached to a unit. After the call the emitter becomes inactive.
	 *
	 * This should be called before dropping the emitter so that the manager
	 * will carry on rendering (until all particles have dissipated)
	 * even when it's no longer attached to a model.
	 */
	void AddUnattachedEmitter(std::unique_ptr<CParticleEmitter> emitter);

	/**
	 * Delete unattached emitters if we don't wish to see them anymore (like in actor viewer)
	 */
	void ClearUnattachedEmitters();

	void RenderSubmit(SceneCollector& collector, const CFrustum& frustum);

	void Interpolate(const float simFrameLength);

	float GetCurrentTime() const { return m_CurrentTime; }

	Status ReloadChangedFile(const VfsPath& path);

	bool ShouldUseInstancing() const { return m_UseInstancing; }

private:
	float m_CurrentTime{0.0f};

	std::vector<std::unique_ptr<CParticleEmitter>> m_UnattachedEmitters;

	std::unordered_map<VfsPath, std::unique_ptr<CParticleEmitterType>> m_EmitterTypes;

	bool m_UseInstancing{false};
};

#endif // INCLUDED_PARTICLEMANAGER
