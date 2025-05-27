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

#ifndef INCLUDED_HEIGHMAPARRAY
#define INCLUDED_HEIGHMAPARRAY
#include "lib/types.h"

#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Conversions.h"
#include "MapEditor_Brushes.h"

namespace JSI_MapEditor
{
	class HeightmapArray : public DeltaArray2D<u16>
	{
	public:
		void Init();
		void RaiseVertex(ssize_t x, ssize_t y, int amount);
		void MoveVertexTowards(ssize_t x, ssize_t y, int target, int amount);
		void SetVertex(ssize_t x, ssize_t y, u16 value);
		u16 GetVertex(ssize_t x, ssize_t y);

		void OverlayWithFromJs(const HeightmapArray& overlayer);
		void AlterElevationFromJs(const Brush* brush, float amount);
		void PickElevationFromJs(const Brush* brush, float amount);
		void SmoothElevationFromJs(const Brush* brush, float amount);
		void FlattenElevationFromJs(const Brush* brush, float amount);
	protected:
		u16 getOld(ssize_t x, ssize_t y)
		{
			return m_Heightmap[y * m_VertsPerSide + x];
		}
		void setNew(ssize_t x, ssize_t y, const u16& val)
		{
			m_Heightmap[y * m_VertsPerSide + x] = val;
		}

		u16* m_Heightmap;
		ssize_t m_VertsPerSide;
	};

	extern JSClass class_HeightmapArray;

	extern JSFunctionSpec methods_HeightmapArray[];
}
#endif // INCLUDED_HEIGHMAPARRAY
