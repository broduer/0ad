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

#include "TerrainArray.h"

#include "graphics/Patch.h"
#include "ps/Game.h"
#include "ps/World.h"
#include "simulation2/helpers/Grid.h"
#include "scriptinterface/Interface.h"

#include <queue>

using namespace JSI_MapEditor;

template <typename T, JSClass* jsClass>
inline T* InstanceGetter(const Script::Request& rq, JS::CallArgs& args)
{
	return Script::Interface::GetPrivate<T>(rq, args);
}

JSClass JSI_MapEditor::class_TerrainTileArray = {
	"TerrainTileArray",
	JSCLASS_HAS_RESERVED_SLOTS(1),
};

JSFunctionSpec JSI_MapEditor::methods_TerrainTileArray[] = {
	Script::Function::Wrap<&TerrainTileArray::Init, InstanceGetter<TerrainTileArray, &JSI_MapEditor::class_TerrainTileArray>>("Init"),
	Script::Function::Wrap<&TerrainTileArray::PaintTilesFromJs, InstanceGetter<TerrainTileArray, &JSI_MapEditor::class_TerrainTileArray>>("PaintTiles"),
	Script::Function::Wrap<&TerrainTileArray::ReplaceTilesFromJs, InstanceGetter<TerrainTileArray, &JSI_MapEditor::class_TerrainTileArray>>("ReplaceTiles"),
	Script::Function::Wrap<&TerrainTileArray::FillTilesFromJs, InstanceGetter<TerrainTileArray, &JSI_MapEditor::class_TerrainTileArray>>("FillTiles"),
	Script::Function::Wrap<&DeltaArray2D<TerrainTile>::Undo, InstanceGetter<DeltaArray2D<TerrainTile>, &JSI_MapEditor::class_TerrainTileArray>>("Undo"),
	Script::Function::Wrap<&DeltaArray2D<TerrainTile>::Redo, InstanceGetter<DeltaArray2D<TerrainTile>, &JSI_MapEditor::class_TerrainTileArray>>("Redo"),
	Script::Function::Wrap<&TerrainTileArray::OverlayWithFromJs, InstanceGetter<TerrainTileArray, &JSI_MapEditor::class_TerrainTileArray>>("OverlayWith"),
	JS_FS_END
};

void TerrainTileArray::Init()
{
	m_Terrain = &g_Game->GetWorld()->GetTerrain();
	m_VertsPerSide = m_Terrain->GetVerticesPerSide();
}

ssize_t TerrainTileArray::GetTilesPerSide()
{
	return m_VertsPerSide;
}

void TerrainTileArray::UpdatePriority(ssize_t x, ssize_t y, CTerrainTextureEntry* tex, int priorityScale, int& priority)
{
	CMiniPatch* tile{m_Terrain->GetTile(x, y)};
	if (!tile)
		return;

	if (tile->GetTextureEntry() == tex)
		priority = std::max(priority, tile->GetPriority() * priorityScale);
	else
		priority = std::max(priority, tile->GetPriority() * priorityScale + 1);
}

CTerrainTextureEntry* TerrainTileArray::GetTexEntry(ssize_t x, ssize_t y)
{
	if(size_t(x) >= size_t(m_VertsPerSide - 1) || size_t(y) >= size_t(m_VertsPerSide - 1))
		return nullptr;

	return get(x, y).tex;
}

void TerrainTileArray::PaintTile(ssize_t x, ssize_t y, CTerrainTextureEntry* tex, int priority)
{
	if (size_t(x) >= size_t(m_VertsPerSide - 1) || size_t(y) >= size_t(m_VertsPerSide - 1))
		return;

	set(x, y, TerrainTile(tex, priority));
}

int TerrainTileArray::GetPriority(ssize_t x, ssize_t y)
{
	if (size_t(x) >= size_t(m_VertsPerSide - 1) || size_t(y) >= size_t(m_VertsPerSide - 1))
		return 0;

	return get(x, y).priority;
}

TerrainTile TerrainTileArray::getOld(ssize_t x, ssize_t y)
{
	CMiniPatch* tile{m_Terrain->GetTile(x, y)};
	ENSURE(tile);
	return TerrainTile(tile->Tex, tile->Priority);
}

void TerrainTileArray::setNew(ssize_t x, ssize_t y, const TerrainTile& val)
{
	CMiniPatch* tile{m_Terrain->GetTile(x, y)};
	ENSURE(tile);

	tile->Tex = val.tex;
	tile->Priority =  val.priority;
}

void TerrainTileArray::PaintTilesFromJs(const std::wstring& textureName, const Brush* brush, int priorityScale)
{
	CTerrainTextureEntry* texEntry{g_TexMan.FindTexture(CStrW(textureName).ToUTF8())};
	if (!texEntry)
	{
		debug_warn(L"Can't find texentry"); // TODO: nicer error handling
		return;
	}

	ssize_t x0, y0;
	brush->GetBottomLeft(x0, y0);

	int priority{0};

	for (ssize_t dy = -1; dy != brush->m_H + 1; ++dy)
	{
		for (ssize_t dx = -1; dx != brush->m_W + 1; ++dx)
		{
			if (!(brush->Get(dx, dy) > 0.5f)) // ignore tiles that will be painted over
				this->UpdatePriority(x0 + dx, y0 + dy, texEntry, priorityScale, priority);
		}
	}

	for (ssize_t dy = 0; dy != brush->m_H; ++dy)
	{
		for (ssize_t dx = 0; dx != brush->m_W; ++dx)
		{
			if (brush->Get(dx, dy) > 0.5f) // TODO: proper solid brushes
				this->PaintTile(x0 + dx, y0 + dy, texEntry, priority * priorityScale);
		}
	}
}

std::vector<int> TerrainTileArray::ReplaceTilesFromJs(const std::wstring& textureName, const Brush* brush)
{
	CTerrainTextureEntry* texEntry{g_TexMan.FindTexture(CStrW(textureName).ToUTF8())};
	if (!texEntry)
	{
		debug_warn(L"Can't find texentry"); // TODO: nicer error handling
		return {};
	}

	ssize_t x0, y0, m_i0, m_j0, m_i1, m_j1;
	brush->GetBottomLeft(x0, y0);

	m_i0 = m_i1 = x0;
	m_j0 = m_j1 = y0;

	CTerrainTextureEntry* replacedTex{this->GetTexEntry(x0,y0)};

	if (texEntry == replacedTex)
		return {};

	ssize_t tiles{this->GetTilesPerSide()};

	for (ssize_t j = 0; j != tiles; ++j)
	{
		for (ssize_t i = 0; i != tiles; ++i)
		{
			if (this->GetTexEntry(i, j) != replacedTex)
				continue;

			m_i0 = std::min(m_i0, i - 1);
			m_j0 = std::min(m_j0, j - 1);
			m_i1 = std::max(m_i1, i + 2);
			m_j1 = std::max(m_j1, j + 2);
			this->PaintTile(i, j, texEntry, this->GetPriority(i,j));
		}
	}

	std::vector<int> ret{
		static_cast<int>(m_i0),
		static_cast<int>(m_j0),
		static_cast<int>(m_i1),
		static_cast<int>(m_j1)
	};
	return ret;
}

std::vector<int> TerrainTileArray::FillTilesFromJs(const std::wstring& textureName, const Brush* brush)
{
	CTerrainTextureEntry* texEntry{g_TexMan.FindTexture(CStrW(textureName).ToUTF8())};
	if (!texEntry)
	{
		debug_warn(L"Can't find texentry"); // TODO: nicer error handling
		return {};
	}

	ssize_t x0, y0, m_i0, m_j0, m_i1, m_j1;
	brush->GetBottomLeft(x0, y0);

	m_i0 = m_i1 = x0;
	m_j0 = m_j1 = y0;

	CTerrainTextureEntry* replacedTex = this->GetTexEntry(x0, y0);

	if (texEntry == replacedTex)
		return {};

	ssize_t tiles{this->GetTilesPerSide()};

	// Simple 4-way flood fill algorithm using queue and a grid to keep track of visited tiles,
		//	almost as fast as loop for filling whole map, much faster for small patches
	SparseGrid<bool> visited(tiles, tiles);
	std::queue<std::pair<u16, u16>> queue;

	// Initial tile
	queue.push(std::make_pair((u16)x0, (u16)y0));
	visited.set(x0, y0, true);

	while (!queue.empty())
	{
		// Check front of queue
		std::pair<u16, u16> t{queue.front()};
		queue.pop();
		u16 i{t.first};
		u16 j{t.second};

		if (this->GetTexEntry(i, j) != replacedTex)
			continue;

		// Found a tile to replace: adjust bounds and paint it
		m_i0 = std::min(m_i0, (ssize_t)i - 1);
		m_j0 = std::min(m_j0, (ssize_t)j - 1);
		m_i1 = std::max(m_i1, (ssize_t)i + 2);
		m_j1 = std::max(m_j1, (ssize_t)j + 2);
		this->PaintTile(i, j, texEntry, this->GetPriority(i, j));

		// Visit 4 adjacent tiles (could visit 8 if we want to count diagonal adjacency)
		if (i > 0 && !visited.get(i - 1, j))
		{
			visited.set(i - 1, j, true);
			queue.push(std::make_pair(i - 1, j));
		}
		if (i < (tiles - 1) && !visited.get(i + 1, j))
		{
			visited.set(i + 1, j, true);
			queue.push(std::make_pair(i + 1, j));
		}
		if (j > 0 && !visited.get(i, j - 1))
		{
			visited.set(i, j - 1, true);
			queue.push(std::make_pair(i, j - 1));
		}
		if (j < (tiles - 1) && !visited.get(i, j + 1))
		{
			visited.set(i, j + 1, true);
			queue.push(std::make_pair(i, j + 1));
		}
	}

	std::vector<int> ret{
		static_cast<int>(m_i0),
		static_cast<int>(m_j0),
		static_cast<int>(m_i1),
		static_cast<int>(m_j1)
	};
	return ret;
}

void TerrainTileArray::OverlayWithFromJs(const TerrainTileArray& overlayer)
{
	OverlayWith(overlayer);
}

