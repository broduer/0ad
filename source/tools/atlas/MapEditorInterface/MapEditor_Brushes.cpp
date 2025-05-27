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

#include "MapEditor_Brushes.h"

#include "graphics/Camera.h"
#include "graphics/Color.h"
#include "graphics/GameView.h"
#include "graphics/Terrain.h"
#include "ps/Game.h"
#include "scriptinterface/Object.h"
#include <scriptinterface/Interface.h>
#include "simulation2/Simulation2.h"

using namespace JSI_MapEditor;

template <typename T, JSClass* jsClass>
inline T* InstanceGetter(const Script::Request& rq, JS::CallArgs& args)
{
	return Script::Interface::GetPrivate<T>(rq, args);
}

JSClass JSI_MapEditor::class_MapEditorBrush = {
	"Brush",
	JSCLASS_HAS_RESERVED_SLOTS(1),
};

JSFunctionSpec JSI_MapEditor::methods_MapEditorBrush[] = {
	Script::Function::Wrap<&Brush::SetData, InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>>("SetData"),
	Script::Function::Wrap<&Brush::SetRenderEnabled, InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>>("SetRenderEnabled"),
	Script::Function::Wrap<&Brush::UpdatePosition, InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>>("UpdatePosition"),
	JS_FN("GetBottomLeft", JSI_MapEditor::GetBottomLeftFromJs, 0 ,0),
	JS_FN("GetWidth", JSI_MapEditor::GetWidthFromJs, 0 ,0),
	JS_FN("GetHeight", JSI_MapEditor::GetHeightFromJs, 0 ,0),
	JS_FN("Get", JSI_MapEditor::GetFromJs, 0 ,2),
	JS_FS_END
};

class BrushTerrainOverlay : public TerrainOverlay
{
public:
	BrushTerrainOverlay(const Brush* brush)
		: TerrainOverlay(g_Game->GetSimulation2()->GetSimContext(), 300), m_Brush(brush)
	{
	}

	void GetTileExtents(
		ssize_t& min_i_inclusive, ssize_t& min_j_inclusive,
		ssize_t& max_i_inclusive, ssize_t& max_j_inclusive)
	{
		m_Brush->GetBottomLeft(min_i_inclusive, min_j_inclusive);
		m_Brush->GetTopRight(max_i_inclusive, max_j_inclusive);

		// But since brushes deal with vertices instead of tiles,
		// we don't want to include the top/right row
		--max_i_inclusive;
		--max_j_inclusive;
	}

	void ProcessTile(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
		ssize_t i, ssize_t j)
	{
		ssize_t i0, j0;
		m_Brush->GetBottomLeft(i0, j0);
		// Color this tile based on the average of the surrounding vertices
		float avg = (
			m_Brush->Get(i - i0, j - j0) + m_Brush->Get(i - i0 + 1, j - j0) +
			m_Brush->Get(i - i0, j - j0 + 1) + m_Brush->Get(i - i0 + 1, j - j0 + 1)
			) / 4.f;
		RenderTile(deviceCommandContext, CColor(0, 1, 0, avg * 0.8f), false);
		if (avg > 0.1f)
			RenderTileOutline(deviceCommandContext, CColor(1, 1, 1, std::min(0.4f, avg - 0.1f)), true);
	}

	const Brush* m_Brush;
};

void Brush::SetData(int w, int h, const std::vector<float>& data)
{
	m_W = w;
	m_H = h;

	m_Data = data;

	ENSURE(data.size() == (size_t)(w * h));
}

void Brush::GetCentre(ssize_t& x, ssize_t& y) const
{
	CVector3D c{m_Centre};
	if (m_W % 2) c.X += TERRAIN_TILE_SIZE / 2.f;
	if (m_H % 2) c.Z += TERRAIN_TILE_SIZE / 2.f;
	ssize_t cx, cy;
	CTerrain::CalcFromPosition(c, cx, cy);

	x = cx;
	y = cy;
}

void Brush::GetBottomLeft(ssize_t& x, ssize_t& y) const
{
	GetCentre(x, y);
	x -= (m_W - 1) / 2;
	y -= (m_H - 1) / 2;
}

void Brush::GetTopRight(ssize_t& x, ssize_t& y) const
{
	GetBottomLeft(x, y);
	x += m_W - 1;
	y += m_H - 1;
}

void Brush::SetRenderEnabled(bool enabled)
{
	if (enabled && !m_TerrainOverlay)
		m_TerrainOverlay.reset(new BrushTerrainOverlay(this));
	else if (!enabled && m_TerrainOverlay)
		m_TerrainOverlay.reset();
}

void Brush::UpdatePosition(int x, int y)
{
	m_Centre = g_Game->GetView()->GetCamera().GetWorldCoordinates(x, y, false);
}

bool JSI_MapEditor::GetBottomLeftFromJs(JSContext* cx, uint argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	const Brush* brush{InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>(Script::Request(cx), args)};
	if (!brush)
		return false;

	ssize_t x, y;
	brush->GetBottomLeft(x, y);

	JS::RootedValue returnValue(cx);
	Script::Request rq(cx);
	Script::CreateObject(rq, &returnValue, "x", static_cast<int>(x), "y", static_cast<int>(y));
	args.rval().set(returnValue);
	return true;
}

bool JSI_MapEditor::GetWidthFromJs(JSContext* cx, uint argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	const Brush* brush{InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>(Script::Request(cx), args)};
	if (!brush)
		return false;

	args.rval().setInt32(brush->m_W);
	return true;
}

bool JSI_MapEditor::GetHeightFromJs(JSContext* cx, uint argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	const Brush* brush{InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>(Script::Request(cx), args)};
	if (!brush)
		return false;

	args.rval().setInt32(brush->m_H);
	return true;
}

bool JSI_MapEditor::GetFromJs(JSContext* cx, uint argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	const Brush* brush{InstanceGetter<Brush, &JSI_MapEditor::class_MapEditorBrush>(Script::Request(cx), args)};
	if (!brush)
		return false;

	if (args.length() != 2)
	{
		Script::Exception::Raise(cx, "Get requires 2 arguments");
		return false;
	}

	const ssize_t x{static_cast<ssize_t>(args[0].toInt32())};
	const ssize_t y{static_cast<ssize_t>(args[1].toInt32())};

	args.rval().setNumber(brush->Get(x, y));
	return true;
}
