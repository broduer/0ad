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

#ifndef INCLUDED_PARTICLEEMITTER
#define INCLUDED_PARTICLEEMITTER

#include "graphics/ModelAbstract.h"
#include "graphics/ParticleEmitterType.h"
#include "graphics/SColor.h"
#include "lib/posix/posix_types.h"
#include "maths/BoundingBoxAligned.h"
#include "maths/Quaternion.h"
#include "maths/Vector3D.h"
#include "renderer/VertexArray.h"

#include <cstddef>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

class CParticleEmitter;
namespace Renderer::Backend { class IDeviceCommandContext; }
namespace Renderer::Backend { class IShaderProgram; }
namespace Renderer::Backend { class IVertexInputLayout; }

/**
 * Simulation state for a single particle.
 */
struct SParticle
{
	CVector3D pos;
	CVector3D velocity;
	float angle;
	float angleSpeed;
	float size;
	float sizeGrowthRate;
	SColor4ub color;
	float age;
	float maxAge;
	CVector3D axisX, axisY;
};

/**
 * Particle emitter.
 *
 * Emitters store particle data in two forms:
 *  * m_Particles contains the raw data used for the CPU particle simulation.
 *  * m_VertexArray contains the data required for rendering.
 * Particles are rendered as billboard quads, so the vertex array contains four vertices
 * per particle with different UV coordinates. The billboard position computation is
 * performed by a vertex shader.
 *
 * The number of particles is a constant for the entire life of the emitter,
 * to simplify the updating and rendering.
 * m_Particles acts like a ring buffer, so we don't have to worry about dynamically
 * allocating particles. If particles have variable lifetimes, they'll exist in the
 * array with alpha=0 until they're overwritten by a new particle after the maximum
 * lifetime.
 *
 * (It's quite likely this could be made more efficient, if the overhead of any added
 * complexity is not high.)
 */
class CParticleEmitter
{
public:
	CParticleEmitter(const CParticleEmitterType& type);

	/**
	 * Set the position to be used for emission of new particles.
	 */
	void SetPosition(const CVector3D& pos)
	{
		m_Pos = pos;
	}

	CVector3D GetPosition() const
	{
		return m_Pos;
	}

	/**
	 * Set the rotation to be used for emission of new particles (note: depends on particles).
	 */
	void SetRotation(const CQuaternion& rot)
	{
		m_Rot = rot;
	}

	const CQuaternion& GetRotation() const
	{
		return m_Rot;
	}

	/**
	 * Get the bounding box of the center points of particles at their current positions.
	 */
	const CBoundingBoxAligned& GetParticleBounds() const { return m_ParticleBounds; }

	/**
	 * Push a new particle onto the ring buffer. (May overwrite an old particle.)
	 */
	void AddParticle(const SParticle& particle);

	/**
	 * Update particle and vertex array data. Must be called before RenderArray.
	 *
	 * If frameNumber is the same as the previous call to UpdateArrayData,
	 * then the function will do no work and return immediately.
	 */
	void UpdateArrayData(int frameNumber);

	/**
	 * Make the vertex data available for subsequent binding and rendering.
	 */
	void PrepareForRendering();

	/**
	 * Upload the vertex data to the backend.
	 */
	void UploadData(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext);

	/**
	 * Bind rendering state (textures and blend modes).
	 */
	void Bind(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		Renderer::Backend::IShaderProgram* shader);

	/**
	 * Draw the vertex array.
	 */
	void RenderArray(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext);

	void SetEntityVariable(const std::string& name, float value);

	const CParticleEmitterType& m_Type;

	/// Whether this emitter is still emitting new particles
	bool m_Active{true};

	CVector3D m_Pos;
	CQuaternion m_Rot;

	std::map<std::string, float> m_EntityVariables;

	/// The source of this emitter's random particle variables.
	std::minstd_rand m_Random;

	std::vector<SParticle> m_Particles;
	uint32_t m_NextParticleIdx{0};
	uint32_t m_NumberOfVisibleParticles{0};

	float m_LastUpdateTime;
	float m_EmissionRoundingError{0.0f};

private:
	/// Bounding box of the current particle center points
	CBoundingBoxAligned m_ParticleBounds;

	VertexIndexArray m_IndexArray;

	VertexArray m_VertexArray;
	VertexArray::Attribute m_AttributePos;
	VertexArray::Attribute m_AttributeColor;
	VertexArray::Attribute m_AttributeAxisX, m_AttributeAxisY;
	VertexArray m_VertexUVArray;
	VertexArray::Attribute m_AttributeUV;

	Renderer::Backend::IVertexInputLayout* m_VertexInputLayout = nullptr;

	int m_LastFrameNumber;
	bool m_UseInstancing{false};
	bool m_Started{false};
};

/**
 * Particle emitter model, for attaching emitters as props on other models.
 */
class CModelParticleEmitter : public CModelAbstract
{
public:
	CModelParticleEmitter(const CParticleEmitterType& type);
	~CModelParticleEmitter() override;

	/// Dynamic cast
	CModelParticleEmitter* ToCModelParticleEmitter() override
	{
		return this;
	}

	std::unique_ptr<CModelAbstract> Clone() const override;

	void SetTerrainDirty(ssize_t /*i0*/, ssize_t /*j0*/, ssize_t /*i1*/, ssize_t /*j1*/) override
	{
	}

	void SetEntityVariable(const std::string& name, float value) override;
	void SetRandomSeed(std::uint32_t seed) override;

	void CalcBounds() override;
	void ValidatePosition() override;
	void InvalidatePosition() override;
	void SetTransform(const CMatrix3D& transform) override;

	const CParticleEmitterType& m_Type;
	std::unique_ptr<CParticleEmitter> m_Emitter;
};

#endif // INCLUDED_PARTICLEEMITTER
