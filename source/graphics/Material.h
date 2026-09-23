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

#ifndef INCLUDED_MATERIAL
#define INCLUDED_MATERIAL

#include "graphics/ShaderDefines.h"
#include "graphics/Texture.h"
#include "ps/CStr.h"
#include "ps/CStrIntern.h"

#include <array>
#include <vector>

class CVector4D;

class CMaterial
{
public:
	enum class Pass : uint8_t
	{
		MAIN,
		SHADOW_CASTER,
		REFLECTIONS,
		REFRACTIONS,
		SILHOUETTE_OCCLUDER,
		SILHOUETTE_CASTER,
		WIREFRAME,
		WIREFRAME_SOLID,
		COUNT
	};

	struct TextureSampler
	{
		TextureSampler(const CStr &n, CTexturePtr t) : Name(n), Sampler(t) {}
		TextureSampler(const CStrIntern &n, CTexturePtr t) : Name(n), Sampler(t) {}

		CStrIntern Name;
		CTexturePtr Sampler;
	};

	struct RequiredSampler
	{
		CStrIntern name;
		bool sRGB;
	};

	typedef std::vector<TextureSampler> SamplersVector;

	CMaterial();

	// Whether this material's shaders use alpha blending, in which case
	// models using this material need to be rendered in a special order
	// relative to the alpha-blended water plane
	void SetUsesAlphaBlending(bool flag) { m_AlphaBlending = flag; m_Identity = 0; }
 	bool UsesAlphaBlending() const { return m_AlphaBlending; }

	const CTexturePtr& GetDiffuseTexture() const { return m_DiffuseTexture; }

	void SetShaderEffect(const Pass pass, CStrIntern effect);
	CStrIntern GetShaderEffect(const Pass pass) const;

	// Must call RecomputeCombinedShaderDefines after this, before rendering with this material
	void AddShaderDefine(CStrIntern key, CStrIntern value);

	const CShaderDefines& GetShaderDefines() const { return m_ShaderDefines; }

	void AddStaticUniform(const char* key, const CVector4D& value);
	const CShaderUniforms& GetStaticUniforms() const { return m_StaticUniforms; }

	void AddSampler(const TextureSampler& texture);
	const SamplersVector& GetSamplers() const { return m_Samplers; }

	void AddRenderQuery(const char* key);
	const CShaderRenderQueries& GetRenderQueries() const { return m_RenderQueries; }

	void AddRequiredSampler(RequiredSampler sampler);
	const std::vector<RequiredSampler>& GetRequiredSamplers() const { return m_RequiredSamplers; }

	/**
	 * Return an ID equal for materials with equal content. Computed on first
	 * use after a change, so only the thread that owns the material may call it.
	 */
	uint64_t GetIdentity() const
	{
		if (!m_Identity)
			m_Identity = ComputeIdentity();
		return m_Identity;
	}

private:
	uint64_t ComputeIdentity() const;

	// This pointer is kept to make it easier for the fixed pipeline to
	// access the only texture it's interested in.
	CTexturePtr m_DiffuseTexture;

	SamplersVector m_Samplers;
	std::vector<RequiredSampler> m_RequiredSamplers;

	std::array<CStrIntern, static_cast<size_t>(Pass::COUNT)> m_ShaderEffects;
	CShaderDefines m_ShaderDefines;
	CShaderUniforms m_StaticUniforms;
	CShaderRenderQueries m_RenderQueries;

	bool m_AlphaBlending;
	mutable uint64_t m_Identity = 0;
};

#endif // INCLUDED_MATERIAL
