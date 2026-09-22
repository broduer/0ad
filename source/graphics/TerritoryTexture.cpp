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

#include "TerritoryTexture.h"

#include "graphics/Color.h"
#include "graphics/Terrain.h"
#include "lib/debug.h"
#include "ps/Profile.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/ITexture.h"
#include "renderer/backend/Sampler.h"
#include "simulation2/components/ICmpPlayer.h"
#include "simulation2/components/ICmpPlayerManager.h"
#include "simulation2/components/ICmpTerrain.h"
#include "simulation2/components/ICmpTerritoryManager.h"
#include "simulation2/helpers/Grid.h"
#include "simulation2/helpers/Pathfinding.h"
#include "simulation2/system/CmpPtr.h"
#include "simulation2/system/Entity.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

// TODO: There's a lot of duplication with CLOSTexture - might be nice to refactor a bit

CTerritoryTexture::CTerritoryTexture(CSimulation2& simulation) :
	m_Simulation(simulation), m_DirtyID(0), m_MapSize(0)
{
}

CTerritoryTexture::~CTerritoryTexture()
{
	DeleteTexture();
}

void CTerritoryTexture::DeleteTexture()
{
	m_Texture.reset();
}

void CTerritoryTexture::Prepare()
{
	CmpPtr<ICmpTerritoryManager> cmpTerritoryManager(m_Simulation, SYSTEM_ENTITY);
	CmpPtr<ICmpTerrain> cmpTerrain(m_Simulation, SYSTEM_ENTITY);
	if (!cmpTerritoryManager || !cmpTerrain || !cmpTerritoryManager->NeedUpdateTexture(&m_DirtyID))
		return;

	PROFILE("generate territory bitmap");

	// Convert size from terrain tiles to territory tiles
	const ssize_t mapSize = cmpTerrain->GetMapSize() * Pathfinding::NAVCELL_SIZE_INT / ICmpTerritoryManager::NAVCELLS_PER_TERRITORY_TILE;
	std::unique_ptr<std::uint8_t[]> bitmap;
	if (cmpTerritoryManager->IsVisible())
	{
		bitmap = std::make_unique<std::uint8_t[]>(mapSize * mapSize * 4);
		GenerateBitmap(cmpTerritoryManager->GetTerritoryGrid(), bitmap.get(), mapSize, mapSize);
	}

	m_Prepared = {true, std::move(bitmap), mapSize};
}

void CTerritoryTexture::TakePublished(Bitmap&& bitmap)
{
	if (bitmap.valid)
		m_Taken = std::move(bitmap);
}

Renderer::Backend::ITexture* CTerritoryTexture::GetTexture()
{
	ENSURE(m_Texture);
	return m_Texture.get();
}

const CMatrix3D& CTerritoryTexture::GetTextureMatrix()
{
	ENSURE(m_Texture);
	return m_TextureMatrix;
}

const CMatrix3D& CTerritoryTexture::GetMinimapTextureMatrix()
{
	ENSURE(m_Texture);
	return m_MinimapTextureMatrix;
}

void CTerritoryTexture::ConstructTexture(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	const uint32_t textureSize = std::bit_ceil(static_cast<uint32_t>(m_MapSize));

	m_Texture = deviceCommandContext->GetDevice()->CreateTexture2D("TerritoryTexture",
		Renderer::Backend::ITexture::Usage::TRANSFER_DST |
			Renderer::Backend::ITexture::Usage::SAMPLED,
		Renderer::Backend::Format::R8G8B8A8_UNORM, textureSize, textureSize,
		Renderer::Backend::Sampler::MakeDefaultSampler(
			Renderer::Backend::Sampler::Filter::LINEAR,
			Renderer::Backend::Sampler::AddressMode::CLAMP_TO_EDGE));

	// Initialise texture with transparency, for the areas we don't
	// overwrite with uploading later.
	std::unique_ptr<std::uint8_t[]> texData = std::make_unique<std::uint8_t[]>(textureSize *
		textureSize * 4);
	memset(texData.get(), 0x00, textureSize * textureSize * 4);
	deviceCommandContext->UploadTexture(
		m_Texture.get(), Renderer::Backend::Format::R8G8B8A8_UNORM,
		texData.get(), textureSize * textureSize * 4);
	texData.reset();

	{
		// Texture matrix: We want to map
		//   world pos (0, y, 0)  (i.e. bottom-left of first tile)
		//     onto texcoord (0, 0)  (i.e. bottom-left of first texel);
		//   world pos (mapsize*cellsize, y, mapsize*cellsize)  (i.e. top-right of last tile)
		//     onto texcoord (mapsize / texsize, mapsize / texsize)  (i.e. top-right of last texel)

		float s = 1.f / static_cast<float>(textureSize * TERRAIN_TILE_SIZE);
		float t = 0.f;
		m_TextureMatrix.SetZero();
		m_TextureMatrix._11 = s;
		m_TextureMatrix._23 = s;
		m_TextureMatrix._14 = t;
		m_TextureMatrix._24 = t;
		m_TextureMatrix._44 = 1;
	}

	{
		// Minimap matrix: We want to map UV (0,0)-(1,1) onto (0,0)-(mapsize/texsize, mapsize/texsize)

		float s = m_MapSize / static_cast<float>(textureSize);
		m_MinimapTextureMatrix.SetZero();
		m_MinimapTextureMatrix._11 = s;
		m_MinimapTextureMatrix._22 = s;
		m_MinimapTextureMatrix._44 = 1;
	}
}

void CTerritoryTexture::GenerateBitmap(const Grid<std::uint8_t>& territories, std::uint8_t* bitmap,
	ssize_t w, ssize_t h)
{
	int alphaMax = 0xC0;
	int alphaFalloff = 0x20;

	CmpPtr<ICmpPlayerManager> cmpPlayerManager(m_Simulation, SYSTEM_ENTITY);

	std::vector<CColor> colors;
	std::int32_t numPlayers = cmpPlayerManager->GetNumPlayers();
	for (std::int32_t p = 0; p < numPlayers; ++p)
	{
		CColor color(1, 0, 1, 1);
		CmpPtr<ICmpPlayer> cmpPlayer(m_Simulation, cmpPlayerManager->GetPlayerByID(p));
		if (cmpPlayer)
			color = cmpPlayer->GetDisplayedColor();
		colors.push_back(color);
	}

	std::uint8_t* p = bitmap;
	for (ssize_t j = 0; j < h; ++j)
		for (ssize_t i = 0; i < w; ++i)
		{
			std::uint8_t val = territories.get(i, j) & ICmpTerritoryManager::TERRITORY_PLAYER_MASK;

			CColor color(1, 0, 1, 1);
			if (val < colors.size())
				color = colors[val];

			*p++ = (int)(color.r * 255.f);
			*p++ = (int)(color.g * 255.f);
			*p++ = (int)(color.b * 255.f);

			// Use alphaMax for borders and gaia territory; these tiles will be deleted later
			if (val == 0 ||
			   (i > 0   && (territories.get(i-1, j) & ICmpTerritoryManager::TERRITORY_PLAYER_MASK) != val) ||
			   (i < w-1 && (territories.get(i+1, j) & ICmpTerritoryManager::TERRITORY_PLAYER_MASK) != val) ||
			   (j > 0   && (territories.get(i, j-1) & ICmpTerritoryManager::TERRITORY_PLAYER_MASK) != val) ||
			   (j < h-1 && (territories.get(i, j+1) & ICmpTerritoryManager::TERRITORY_PLAYER_MASK) != val))
				*p++ = alphaMax;
			else
				*p++ = 0x00;
		}

	// Do a low-quality cheap blur effect

	for (ssize_t j = 0; j < h; ++j)
	{
		int a;

		a = 0;
		for (ssize_t i = 0; i < w; ++i)
		{
			a = std::max(a - alphaFalloff, (int)bitmap[(j*w+i)*4 + 3]);
			bitmap[(j*w+i)*4 + 3] = a;
		}

		a = 0;
		for (ssize_t i = w-1; i >= 0; --i)
		{
			a = std::max(a - alphaFalloff, (int)bitmap[(j*w+i)*4 + 3]);
			bitmap[(j*w+i)*4 + 3] = a;
		}
	}

	for (ssize_t i = 0; i < w; ++i)
	{
		int a;

		a = 0;
		for (ssize_t j = 0; j < w; ++j)
		{
			a = std::max(a - alphaFalloff, (int)bitmap[(j*w+i)*4 + 3]);
			bitmap[(j*w+i)*4 + 3] = a;
		}

		a = 0;
		for (ssize_t j = w-1; j >= 0; --j)
		{
			a = std::max(a - alphaFalloff, (int)bitmap[(j*w+i)*4 + 3]);
			bitmap[(j*w+i)*4 + 3] = a;
		}
	}

	// Add a gap between the boundaries, by deleting the max-alpha tiles
	for (ssize_t j = 0; j < h; ++j)
		for (ssize_t i = 0; i < w; ++i)
			if (bitmap[(j*w+i)*4 + 3] == alphaMax)
				bitmap[(j*w+i)*4 + 3] = 0;
}

void CTerritoryTexture::UpdateIfNeeded(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (!m_Taken.valid)
		return;
	Bitmap taken = std::exchange(m_Taken, Bitmap{});
	std::unique_ptr<std::uint8_t[]> bitmap = std::move(taken.data);
	const ssize_t mapSize = taken.mapSize;

	if (m_Texture && mapSize != m_MapSize)
		DeleteTexture();
	m_MapSize = mapSize;
	if (!m_Texture)
		ConstructTexture(deviceCommandContext);

	if (!bitmap)
		return;

	PROFILE("upload territory texture");

	deviceCommandContext->UploadTextureRegion(
		m_Texture.get(), Renderer::Backend::Format::R8G8B8A8_UNORM, bitmap.get(), m_MapSize * m_MapSize * 4,
		0, 0, m_MapSize, m_MapSize);
}
