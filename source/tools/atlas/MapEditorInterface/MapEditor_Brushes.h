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

#ifndef INCLUDED_MAPEDITOR_BRUSHES
#define INCLUDED_MAPEDITOR_BRUSHES

#include "maths/Vector3D.h"
#include "renderer/TerrainOverlay.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Conversions.h"

#include <vector>

namespace JSI_MapEditor
{
	class Brush
	{
	public:
		Brush() = default;
		~Brush() = default;

		void SetData(int w, int h, const std::vector<float>& data);

		// Initial state is disabled.
		void SetRenderEnabled(bool enabled);

		void UpdatePosition(int x, int y);

		void GetCentre(ssize_t& x, ssize_t& y) const;
		void GetBottomLeft(ssize_t& x, ssize_t& y) const;
		void GetTopRight(ssize_t& x, ssize_t& y) const;

		float Get(ssize_t x, ssize_t y) const
		{
			if (x >= 0 && x < m_W && y >= 0 && y < m_H)
				return m_Data[x + y * m_W];
			else
				return 0.f;
		}

		ssize_t m_W{0};
		ssize_t m_H{0};
		CVector3D m_Centre;
	private:
		// NULL if rendering is not enabled.
		std::unique_ptr<TerrainOverlay> m_TerrainOverlay{nullptr};
		std::vector<float> m_Data;
	};

	extern JSClass class_MapEditorBrush;

	extern JSFunctionSpec methods_MapEditorBrush[];

	bool GetBottomLeftFromJs(JSContext* cx, uint argc, JS::Value* vp);
	bool GetFromJs(JSContext* cx, uint argc, JS::Value* vp);
	bool GetWidthFromJs(JSContext* cx, uint argc, JS::Value* vp);
	bool GetHeightFromJs(JSContext* cx, uint argc, JS::Value* vp);
}

#endif // INCLUDED_MAPEDITOR_BRUSHES
