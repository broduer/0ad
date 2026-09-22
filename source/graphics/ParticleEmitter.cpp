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

#include "ParticleEmitter.h"

#include "graphics/LOSTexture.h"
#include "graphics/LightEnv.h"
#include "graphics/ParticleEmitterType.h"
#include "graphics/ParticleManager.h"
#include "graphics/RenderableObject.h"
#include "graphics/TextureManager.h"
#include "lib/allocators/STLAllocators.h"
#include "lib/debug.h"
#include "maths/Matrix3D.h"
#include "ps/memory/LinearAllocator.h"
#include "ps/CStrIntern.h"
#include "ps/CStrInternStatic.h"
#include "renderer/Renderer.h"
#include "renderer/Scene.h"
#include "renderer/SceneRenderer.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/IShaderProgram.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace
{

struct ParticleYoungestInFrontCompare
{
	bool operator()(const SParticle& lhs, const SParticle& rhs) const
	{
		return lhs.age > rhs.age;
	}
};

struct ParticleOldestInFrontCompare
{
	bool operator()(const SParticle& lhs, const SParticle& rhs) const
	{
		return lhs.age < rhs.age;
	}
};

struct ParticleClosestInFrontCompare
{
	bool operator()(const SParticle& lhs, const SParticle& rhs) const
	{
		return cameraForward.Dot(lhs.pos) > cameraForward.Dot(rhs.pos);
	}

	CVector3D cameraForward;
};

} // anonymous namespace

CParticleEmitter::CParticleEmitter(const CParticleEmitterType& type) :
	m_Type(type), m_LastUpdateTime(0.0f),
	m_IndexArray(Renderer::Backend::IBuffer::Usage::TRANSFER_DST),
	m_VertexArray(Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::DYNAMIC | Renderer::Backend::IBuffer::Usage::TRANSFER_DST),
	m_VertexUVArray(Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::TRANSFER_DST),
	m_LastFrameNumber(-1), m_UseInstancing{type.m_Manager.ShouldUseInstancing()}
{
	m_Particles.reserve(m_Type.m_MaxParticles);

	m_AttributePos.format = Renderer::Backend::Format::R32G32B32_SFLOAT;
	m_VertexArray.AddAttribute(&m_AttributePos);

	m_AttributeColor.format = Renderer::Backend::Format::R8G8B8A8_UNORM;
	m_VertexArray.AddAttribute(&m_AttributeColor);

	m_AttributeAxisX.format = Renderer::Backend::Format::R32G32B32A32_SFLOAT;
	m_VertexArray.AddAttribute(&m_AttributeAxisX);

	m_AttributeAxisY.format = Renderer::Backend::Format::R32G32B32A32_SFLOAT;
	m_VertexArray.AddAttribute(&m_AttributeAxisY);

	m_VertexArray.SetNumberOfVertices(m_UseInstancing ? m_Type.m_MaxParticles : m_Type.m_MaxParticles * 4);
	m_VertexArray.Layout();

	m_AttributeUV.format = Renderer::Backend::Format::R32G32_SFLOAT;
	m_VertexUVArray.AddAttribute(&m_AttributeUV);

	m_VertexUVArray.SetNumberOfVertices(m_UseInstancing ? 4 : m_Type.m_MaxParticles * 4);
	m_VertexUVArray.Layout();

	VertexArrayIterator<float[2]> attrUV = m_AttributeUV.GetIterator<float[2]>();
	for (uint32_t index{0}; index < (m_UseInstancing ? 1u : m_Type.m_MaxParticles); ++index)
	{
		(*attrUV)[0] = 1;
		(*attrUV)[1] = 0;
		++attrUV;
		(*attrUV)[0] = 0;
		(*attrUV)[1] = 0;
		++attrUV;
		(*attrUV)[0] = 0;
		(*attrUV)[1] = 1;
		++attrUV;
		(*attrUV)[0] = 1;
		(*attrUV)[1] = 1;
		++attrUV;
	}

	m_IndexArray.SetNumberOfVertices(m_UseInstancing ? 6 : m_Type.m_MaxParticles * 6);
	m_IndexArray.Layout();
	VertexArrayIterator<std::uint16_t> index = m_IndexArray.GetIterator();
	for (std::uint16_t i = 0; i < (m_UseInstancing ? 1 : m_Type.m_MaxParticles); ++i)
	{
		*index++ = i*4 + 0;
		*index++ = i*4 + 1;
		*index++ = i*4 + 2;
		*index++ = i*4 + 2;
		*index++ = i*4 + 3;
		*index++ = i*4 + 0;
	}

	const uint32_t stride{m_VertexArray.GetStride()};
	const Renderer::Backend::VertexAttributeRate dynamicDataRate{
		m_UseInstancing
			? Renderer::Backend::VertexAttributeRate::PER_INSTANCE
			: Renderer::Backend::VertexAttributeRate::PER_VERTEX};
	const std::array<Renderer::Backend::SVertexAttributeFormat, 5> attributes{{
		{Renderer::Backend::VertexAttributeStream::UV0,
			m_AttributeUV.format, m_AttributeUV.offset, m_VertexUVArray.GetStride(),
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0},
		{Renderer::Backend::VertexAttributeStream::POSITION,
			m_AttributePos.format, m_AttributePos.offset, stride,
			dynamicDataRate, 1},
		{Renderer::Backend::VertexAttributeStream::COLOR,
			m_AttributeColor.format, m_AttributeColor.offset, stride,
			dynamicDataRate, 1},
		{Renderer::Backend::VertexAttributeStream::UV1,
			m_AttributeAxisX.format, m_AttributeAxisX.offset, stride,
			dynamicDataRate, 1},
		{Renderer::Backend::VertexAttributeStream::UV2,
			m_AttributeAxisY.format, m_AttributeAxisY.offset, stride,
			dynamicDataRate, 1},
	}};
	m_VertexInputLayout = g_Renderer.GetVertexInputLayout(attributes);
}

void CParticleEmitter::UpdateArrayData(int frameNumber)
{
	if (m_LastFrameNumber == frameNumber)
		return;

	// The emitter may be built on the logic thread, which must not touch the
	// device or the render thread's particle clock, so upload the fixed data
	// and start the clock on first use.
	if (!m_Started)
	{
		m_VertexUVArray.Upload();
		m_VertexUVArray.FreeBackingStore();
		m_IndexArray.Upload();
		m_IndexArray.FreeBackingStore();

		m_LastUpdateTime = m_Type.m_Manager.GetCurrentTime();
		// If we should start with particles fully emitted, pretend that we
		// were created in the past so the first update will produce lots of
		// particles.
		// TODO: instead of this, maybe it would make more sense to do a full
		// lifetime-length update of all emitters when the game first starts
		// (so that e.g. buildings constructed later on won't have fully-started
		// emitters, but those at the start will)?
		if (m_Type.m_StartFull)
			m_LastUpdateTime -= m_Type.m_MaxLifetime;
		m_Started = true;
	}

	PS::Memory::ScopedLinearAllocator scopedLinearAllocator{g_Renderer.GetLinearAllocator()};

	using ParticlesVector = std::vector<SParticle, ProxyAllocator<SParticle, PS::Memory::ScopedLinearAllocator>>;
	ParticlesVector sortedParticles((ParticlesVector::allocator_type(scopedLinearAllocator)));

	m_LastFrameNumber = frameNumber;

	// Update m_Particles
	m_Type.UpdateEmitter(*this, m_Type.m_Manager.GetCurrentTime() - m_LastUpdateTime);
	m_LastUpdateTime = m_Type.m_Manager.GetCurrentTime();

	// Regenerate the vertex array data:

	VertexArrayIterator<CVector3D> attrPos = m_AttributePos.GetIterator<CVector3D>();
	VertexArrayIterator<SColor4ub> attrColor = m_AttributeColor.GetIterator<SColor4ub>();
	VertexArrayIterator<CVector4D> attrAxisX = m_AttributeAxisX.GetIterator<CVector4D>();
	VertexArrayIterator<CVector4D> attrAxisY = m_AttributeAxisY.GetIterator<CVector4D>();

	ENSURE(m_Particles.size() <= m_Type.m_MaxParticles);

	CBoundingBoxAligned bounds;

	if (m_Type.m_SortMode != CParticleEmitterType::SortMode::UNSPECIFIED)
	{
		sortedParticles.insert(sortedParticles.end(), m_Particles.begin(), m_Particles.end());

		switch (m_Type.m_SortMode)
		{
		case CParticleEmitterType::SortMode::YOUNGEST_IN_FRONT:
			std::sort(sortedParticles.begin(), sortedParticles.end(), ParticleYoungestInFrontCompare{});
			break;
		case CParticleEmitterType::SortMode::OLDEST_IN_FRONT:
			std::sort(sortedParticles.begin(), sortedParticles.end(), ParticleOldestInFrontCompare{});
			break;
		case CParticleEmitterType::SortMode::CLOSEST_IN_FRONT:
			std::sort(sortedParticles.begin(), sortedParticles.end(), ParticleClosestInFrontCompare{
				g_Renderer.GetSceneRenderer().GetViewCamera().GetOrientation().GetIn()});
			break;
		default:
			break;
		}
	}

	m_NumberOfVisibleParticles = 0;

	const std::span<SParticle> particles{
		m_Type.m_SortMode == CParticleEmitterType::SortMode::UNSPECIFIED ?
			std::span<SParticle>{m_Particles} : std::span<SParticle>{sortedParticles}};

	if (m_UseInstancing)
	{
		for (const SParticle& particle : particles)
		{
			if (particle.age > particle.maxAge || particle.color.A == 0)
				continue;

			++m_NumberOfVisibleParticles;

			bounds += particle.pos;

			*attrPos++ = particle.pos;

			SColor4ub color{particle.color};

			// Special case: If the blending depends on the source color, not the source alpha,
			// then pre-multiply by the alpha. (This is kind of a hack.)
			if (m_Type.m_BlendMode == CParticleEmitterType::BlendMode::OVERLAY ||
				m_Type.m_BlendMode == CParticleEmitterType::BlendMode::MULTIPLY)
			{
				color.R = (color.R * color.A) / 255;
				color.G = (color.G * color.A) / 255;
				color.B = (color.B * color.A) / 255;
			}

			*attrColor++ = color;

			const CVector4D axisXAndSize{
				particle.axisX.X, particle.axisX.Y, particle.axisX.Z, particle.size};
			const CVector4D axisYAndAngle{
				particle.axisY.X, particle.axisY.Y, particle.axisY.Z, particle.angle};

			*attrAxisX++ = axisXAndSize;
			*attrAxisY++ = axisYAndAngle;
		}
	}
	else
	{
		for (const SParticle& particle : particles)
		{
			if (particle.age > particle.maxAge || particle.color.A == 0)
				continue;

			++m_NumberOfVisibleParticles;

			bounds += particle.pos;

			*attrPos++ = particle.pos;
			*attrPos++ = particle.pos;
			*attrPos++ = particle.pos;
			*attrPos++ = particle.pos;

			SColor4ub color{particle.color};

			// Special case: If the blending depends on the source color, not the source alpha,
			// then pre-multiply by the alpha. (This is kind of a hack.)
			if (m_Type.m_BlendMode == CParticleEmitterType::BlendMode::OVERLAY ||
				m_Type.m_BlendMode == CParticleEmitterType::BlendMode::MULTIPLY)
			{
				color.R = (color.R * color.A) / 255;
				color.G = (color.G * color.A) / 255;
				color.B = (color.B * color.A) / 255;
			}

			*attrColor++ = color;
			*attrColor++ = color;
			*attrColor++ = color;
			*attrColor++ = color;

			const CVector4D axisXAndSize{
				particle.axisX.X, particle.axisX.Y, particle.axisX.Z, particle.size};
			const CVector4D axisYAndAngle{
				particle.axisY.X, particle.axisY.Y, particle.axisY.Z, particle.angle};

			*attrAxisX++ = axisXAndSize;
			*attrAxisX++ = axisXAndSize;
			*attrAxisX++ = axisXAndSize;
			*attrAxisX++ = axisXAndSize;

			*attrAxisY++ = axisYAndAngle;
			*attrAxisY++ = axisYAndAngle;
			*attrAxisY++ = axisYAndAngle;
			*attrAxisY++ = axisYAndAngle;
		}
	}

	m_ParticleBounds = bounds;

	m_VertexArray.Upload();
}

void CParticleEmitter::PrepareForRendering()
{
	m_VertexArray.PrepareForRendering();
	m_VertexUVArray.PrepareForRendering();
}

void CParticleEmitter::UploadData(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	m_VertexArray.UploadIfNeeded(deviceCommandContext);
	m_VertexUVArray.UploadIfNeeded(deviceCommandContext);
}

void CParticleEmitter::Bind(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	Renderer::Backend::IShaderProgram* shader)
{
	m_Type.m_Texture->UploadBackendTextureIfNeeded(deviceCommandContext);

	CLOSTexture& los = g_Renderer.GetSceneRenderer().GetScene().GetLOSTexture();
	deviceCommandContext->SetTexture(
		shader->GetBindingSlot(str_losTex), los.GetTextureSmooth());
	deviceCommandContext->SetUniform(
		shader->GetBindingSlot(str_losTransform),
		los.GetTextureMatrix()[0], los.GetTextureMatrix()[12]);

	g_Renderer.GetSceneRenderer().GetLightEnv().Bind(deviceCommandContext, shader);

	deviceCommandContext->SetTexture(
		shader->GetBindingSlot(str_baseTex), m_Type.m_Texture->GetBackendTexture());
}

void CParticleEmitter::RenderArray(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (m_NumberOfVisibleParticles == 0)
		return;

	const uint32_t stride = m_VertexArray.GetStride();
	const uint32_t firstVertexOffset = m_VertexArray.GetOffset() * stride;

	deviceCommandContext->SetVertexInputLayout(m_VertexInputLayout);

	deviceCommandContext->SetVertexBuffer(
		0, m_VertexUVArray.GetBuffer(),
		m_VertexUVArray.GetStride() * m_VertexUVArray.GetOffset());
	deviceCommandContext->SetVertexBuffer(
		1, m_VertexArray.GetBuffer(), firstVertexOffset);
	deviceCommandContext->SetIndexBuffer(m_IndexArray.GetBuffer());

	if (m_UseInstancing)
		deviceCommandContext->DrawIndexedInstanced(m_IndexArray.GetOffset(), 6, 0, m_NumberOfVisibleParticles, 0);
	else
		deviceCommandContext->DrawIndexed(m_IndexArray.GetOffset(), m_NumberOfVisibleParticles * 6, 0);

	g_Renderer.GetStats().m_DrawCalls++;
	g_Renderer.GetStats().m_Particles += m_NumberOfVisibleParticles;
}

void CParticleEmitter::AddParticle(const SParticle& particle)
{
	if (m_NextParticleIdx >= m_Particles.size())
		m_Particles.push_back(particle);
	else
		m_Particles[m_NextParticleIdx] = particle;

	m_NextParticleIdx = (m_NextParticleIdx + 1) % m_Type.m_MaxParticles;
}

void CParticleEmitter::SetEntityVariable(const std::string& name, float value)
{
	m_EntityVariables[name] = value;
}

CModelParticleEmitter::CModelParticleEmitter(const CParticleEmitterType& type)
	: m_Type{type}, m_Emitter{std::make_unique<CParticleEmitter>(m_Type)}
{
}

CModelParticleEmitter::~CModelParticleEmitter()
{
	m_Type.m_Manager.AddUnattachedEmitter(std::move(m_Emitter));
}

void CModelParticleEmitter::SetEntityVariable(const std::string& name, float value)
{
	m_Emitter->SetEntityVariable(name, value);
}

void CModelParticleEmitter::SetRandomSeed(const std::uint32_t seed)
{
	m_Emitter->m_Random.seed(seed);
}

std::unique_ptr<CModelAbstract> CModelParticleEmitter::Clone() const
{
	return std::make_unique<CModelParticleEmitter>(m_Type);
}

void CModelParticleEmitter::CalcBounds()
{
	// TODO: we ought to compute sensible bounds here, probably based on the
	// current computed particle positions plus the emitter type's largest
	// potential bounding box at the current position

	m_WorldBounds = m_Type.CalculateBounds(m_Emitter->GetPosition(), m_Emitter->GetParticleBounds());
}

void CModelParticleEmitter::ValidatePosition()
{
	// TODO: do we need to do anything here?

	// This is a convenient (though possibly not particularly appropriate) place
	// to invalidate bounds so they'll be recomputed from the recent particle data
	InvalidateBounds();
}

void CModelParticleEmitter::InvalidatePosition()
{
}

void CModelParticleEmitter::SetTransform(const CMatrix3D& transform)
{
	if (m_Transform == transform)
		return;

	m_Emitter->SetPosition(transform.GetTranslation());
	m_Emitter->SetRotation(transform.GetRotation());

	// call base class to set transform on this object
	CRenderableObject::SetTransform(transform);
}
