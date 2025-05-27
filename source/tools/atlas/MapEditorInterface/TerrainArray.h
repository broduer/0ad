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

#include "DeltaArray.h"

#include "graphics/Terrain.h"
#include "graphics/TerrainTextureEntry.h"
#include "MapEditor_Brushes.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Conversions.h"

#ifndef INCLUDED_TERRAINARRAY
#define INCLUDED_TERRAINARRAY

namespace JSI_MapEditor
{
	struct TerrainTile
	{
		TerrainTile(CTerrainTextureEntry* t, int p) : tex(t), priority(p) {}
		CTerrainTextureEntry* tex;
		int priority;
	};

	class TerrainTileArray : public DeltaArray2D<TerrainTile>
	{
	public:
		void Init();
		void UpdatePriority(ssize_t x, ssize_t y, CTerrainTextureEntry* tex, int priorityScale, int& priority);

		CTerrainTextureEntry* GetTexEntry(ssize_t x, ssize_t y);

		int GetPriority(ssize_t x, ssize_t y);

		void PaintTile(ssize_t x, ssize_t y, CTerrainTextureEntry* tex, int priority);
		void PaintTilesFromJs(const std::wstring& textureName, const Brush* brush, int priorityScale);
		std::vector<int> ReplaceTilesFromJs(const std::wstring& textureName, const Brush* brush);
		std::vector<int> FillTilesFromJs(const std::wstring& textureName, const Brush* brush);

		void OverlayWithFromJs(const TerrainTileArray& overlayer);

		ssize_t GetTilesPerSide();
	protected:
		TerrainTile getOld(ssize_t x, ssize_t y) override;
		void setNew(ssize_t x, ssize_t y, const TerrainTile& val) override;
		ssize_t m_VertsPerSide;
		CTerrain* m_Terrain;
	};

	extern JSClass class_TerrainTileArray;

	extern JSFunctionSpec methods_TerrainTileArray[];
}

#endif // INCLUDED_TERRAINARRAY
