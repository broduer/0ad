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

#include "Material.h"

#include "ps/CStrInternStatic.h"

#include <map>
#include <mutex>
#include <vector>

class CVector4D;

CMaterial::CMaterial() :
	m_AlphaBlending(false)
{
}

CStrIntern CMaterial::GetShaderEffect(const Pass pass) const
{
	ENSURE(pass < Pass::COUNT);
	return m_ShaderEffects[static_cast<uint8_t>(pass)];
}

void CMaterial::SetShaderEffect(const Pass pass, CStrIntern effect)
{
	m_Identity = 0;
	ENSURE(pass < Pass::COUNT);
	m_ShaderEffects[static_cast<uint8_t>(pass)] = effect;
}

void CMaterial::AddShaderDefine(CStrIntern key, CStrIntern value)
{
	m_Identity = 0;
	m_ShaderDefines.Add(key, value);
}

void CMaterial::AddStaticUniform(const char* key, const CVector4D& value)
{
	m_Identity = 0;
	m_StaticUniforms.Add(key, value);
}

void CMaterial::AddSampler(const TextureSampler& texture)
{
	m_Identity = 0;
	m_Samplers.push_back(texture);
	if (texture.Name == str_baseTex)
		m_DiffuseTexture = texture.Sampler;
}

void CMaterial::AddRenderQuery(const char* key)
{
	m_Identity = 0;
	m_RenderQueries.Add(key);
}

void CMaterial::AddRequiredSampler(RequiredSampler sampler)
{
	m_Identity = 0;
	m_RequiredSamplers.emplace_back(std::move(sampler));
}

uint64_t CMaterial::ComputeIdentity() const
{
	// Key on everything the renderer reads; interned and shared parts compare
	// by pointer.
	std::vector<uintptr_t> key;
	key.reserve(8 + m_Samplers.size() * 2 + m_RenderQueries.GetSize() * 2 + m_RequiredSamplers.size());
	key.push_back(m_AlphaBlending);
	for (const CStrIntern& effect : m_ShaderEffects)
		key.push_back(reinterpret_cast<uintptr_t>(effect.c_str()));
	key.push_back(reinterpret_cast<uintptr_t>(m_ShaderDefines.GetInterned()));
	key.push_back(reinterpret_cast<uintptr_t>(m_StaticUniforms.GetInterned()));
	key.push_back(reinterpret_cast<uintptr_t>(m_DiffuseTexture.get()));
	for (const TextureSampler& sampler : m_Samplers)
	{
		key.push_back(reinterpret_cast<uintptr_t>(sampler.Name.c_str()));
		key.push_back(reinterpret_cast<uintptr_t>(sampler.Sampler.get()));
	}
	for (size_t i = 0; i < m_RenderQueries.GetSize(); ++i)
	{
		const CShaderRenderQueries::RenderQuery query = m_RenderQueries.GetItem(i);
		key.push_back(static_cast<uintptr_t>(query.first));
		key.push_back(reinterpret_cast<uintptr_t>(query.second.c_str()));
	}
	for (const RequiredSampler& sampler : m_RequiredSamplers)
	{
		key.push_back(reinterpret_cast<uintptr_t>(sampler.name.c_str()));
		key.push_back(sampler.sRGB);
	}

	static std::mutex mutex;
	static std::map<std::vector<uintptr_t>, uint64_t> identities;
	std::lock_guard<std::mutex> lock(mutex);
	const auto [it, inserted] = identities.try_emplace(std::move(key), identities.size() + 1);
	return it->second;
}
