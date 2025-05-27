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

#include "HeightmapArray.h"

#include "graphics/Terrain.h"
#include "maths/MathUtil.h"
#include "ps/Game.h"
#include "ps/World.h"
#include "scriptinterface/Interface.h"

#include <vector>
#include <cmath>

using namespace JSI_MapEditor;

template <typename T, JSClass* jsClass>
inline T* InstanceGetter(const Script::Request& rq, JS::CallArgs& args)
{
	return Script::Interface::GetPrivate<T>(rq, args);
}

JSClass JSI_MapEditor::class_HeightmapArray = {
	"HeightmapArray",
	JSCLASS_HAS_RESERVED_SLOTS(1),
};

JSFunctionSpec JSI_MapEditor::methods_HeightmapArray[] = {
	Script::Function::Wrap<&HeightmapArray::Init, InstanceGetter<HeightmapArray, &JSI_MapEditor::class_HeightmapArray>>("Init"),
	Script::Function::Wrap<&HeightmapArray::AlterElevationFromJs, InstanceGetter<HeightmapArray, &JSI_MapEditor::class_HeightmapArray>>("AlterElevation"),
	Script::Function::Wrap<&HeightmapArray::PickElevationFromJs, InstanceGetter<HeightmapArray, &JSI_MapEditor::class_HeightmapArray>>("PickElevation"),
	Script::Function::Wrap<&HeightmapArray::SmoothElevationFromJs, InstanceGetter<HeightmapArray, &JSI_MapEditor::class_HeightmapArray>>("SmoothElevation"),
	Script::Function::Wrap<&HeightmapArray::FlattenElevationFromJs, InstanceGetter<HeightmapArray, &JSI_MapEditor::class_HeightmapArray>>("FlattenElevation"),
	Script::Function::Wrap<&DeltaArray2D<u16>::Undo, InstanceGetter<DeltaArray2D<u16>, &JSI_MapEditor::class_HeightmapArray>>("Undo"),
	Script::Function::Wrap<&DeltaArray2D<u16>::Redo, InstanceGetter<DeltaArray2D<u16>, &JSI_MapEditor::class_HeightmapArray>>("Redo"),
	Script::Function::Wrap<&HeightmapArray::OverlayWithFromJs, InstanceGetter<HeightmapArray, &JSI_MapEditor::class_HeightmapArray>>("OverlayWith"),
	JS_FS_END
};

void HeightmapArray::Init()
{
	m_Heightmap = g_Game->GetWorld()->GetTerrain().GetHeightMap();
	m_VertsPerSide = g_Game->GetWorld()->GetTerrain().GetVerticesPerSide();
}

void HeightmapArray::RaiseVertex(ssize_t x, ssize_t y, int amount)
{
	// Ignore out-of-bounds vertices.
	if (size_t(x) >= size_t(m_VertsPerSide) || size_t(y) >= size_t(m_VertsPerSide))
		return;

	set(x, y, static_cast<u16>(Clamp(get(x, y) + amount, 0, 65535)));
}


void HeightmapArray::MoveVertexTowards(ssize_t x, ssize_t y, int target, int amount)
{
	if (size_t(x) >= size_t(m_VertsPerSide) || size_t(y) >= size_t(m_VertsPerSide))
		return;

	int h{get(x, y)};
	if (h < target)
		h = std::min(target, h + amount);
	else if (h > target)
		h = std::max(target, h - amount);
	else
		return;

	set(x, y, static_cast<u16>(Clamp(h, 0, 65535)));
}

void HeightmapArray::SetVertex(ssize_t x, ssize_t y, u16 value)
{
	if (size_t(x) >= size_t(m_VertsPerSide) || size_t(y) >= size_t(m_VertsPerSide))
		return;

	set(x, y, value);
}

u16 HeightmapArray::GetVertex(ssize_t x, ssize_t y)
{
	return get(Clamp<ssize_t>(x, 0, m_VertsPerSide - 1), Clamp<ssize_t>(y, 0, m_VertsPerSide - 1));
}

void HeightmapArray::OverlayWithFromJs(const HeightmapArray& overlayer)
{
	OverlayWith(overlayer);
}

void HeightmapArray::AlterElevationFromJs(const Brush* brush, float amount)
{
	// If the framerate is very high, 'amount' is often very
	// small (even zero) so the integer truncation is significant.
	static float roundingError{0.0f};
	roundingError += amount - std::truncf(amount);
	if (roundingError >= 1.f)
	{
		amount += static_cast<int>(roundingError);
		roundingError -= std::truncf(roundingError);
	}

	ssize_t x0, y0;
	brush->GetBottomLeft(x0, y0);

	for (ssize_t dy{0}; dy != brush->m_H; ++dy)
	{
		for (ssize_t dx{0}; dx != brush->m_W; ++dx)
		{
			// TODO: proper variable raise amount (store floats in terrain delta array?).
			float b{brush->Get(dx, dy)};
			if (!b)
				continue;

			this->RaiseVertex(x0 + dx, y0 + dy, static_cast<int>((amount * b)));
		}
	}
}

void HeightmapArray::PickElevationFromJs(const Brush* brush, float amount)
{
	// If the framerate is very high, 'amount' is often very
	// small (even zero) so the integer truncation is significant.
	static float roundingError{0.0f};
	roundingError += amount - std::truncf(amount);
	if (roundingError >= 1.f)
	{
		amount += static_cast<int>(roundingError);
		roundingError -= std::truncf(roundingError);
	}

	ssize_t x0, y0;
	brush->GetBottomLeft(x0, y0);

	float h{(static_cast<float>(brush->m_H - 1)) / 2.f};

	for (ssize_t dy{0}; dy != brush->m_H; ++dy)
	{
		for (ssize_t dx{0}; dx != brush->m_W; ++dx)
		{
			float b{brush->Get(dx, dy)};
			if (!b)
				continue;

			const float x{static_cast<float>(dx) - (static_cast<float>(brush->m_H - 1)) / 2.f};
			const float y{static_cast<float>(dy) - (static_cast<float>(brush->m_W - 1)) / 2.f};
			float distance{Clamp(1 - static_cast<float>(sqrt(x * x + y * y)) / h, 0.01f, 1.0f)};
			distance *= distance;
			this->RaiseVertex(x0 + dx, y0 + dy, static_cast<int>(amount * distance));
		}
	}
}

void HeightmapArray::SmoothElevationFromJs(const Brush* brush, float amount)
{
	// If the framerate is very high, 'amount' is often very
	// small (even zero) so the integer truncation is significant.
	static float roundingError = 0.0;
	roundingError += amount - std::truncf(amount);
	if (roundingError >= 1.f)
	{
		amount += static_cast<int>(roundingError);
		roundingError -= std::truncf(roundingError);
	}

	ssize_t x0, y0;
	brush->GetBottomLeft(x0, y0);

	if (brush->m_H <= 2)
		return;

	std::vector<float> terrainDeltas;
	ssize_t num{(brush->m_H - 2) * (brush->m_W - 2)};
	terrainDeltas.resize(num);

	// For each vertex, compute the average of the 9 adjacent vertices.
	for (ssize_t dy{0}; dy != brush->m_H; ++dy)
	{
		for (ssize_t dx{0}; dx != brush->m_W; ++dx)
		{
			const float delta{this->GetVertex(x0 + dx, y0 + dy) / 9.0f};
			const ssize_t x1_min{std::max(static_cast<ssize_t>(1), dx - 1)};
			const ssize_t x1_max{std::min(dx + 1, brush->m_W - 2)};
			const ssize_t y1_min{std::max(static_cast<ssize_t>(1), dy - 1)};
			const ssize_t y1_max{std::min(dy + 1, brush->m_H - 2)};

			for (ssize_t yy{y1_min}; yy != y1_max + 1; ++yy)
			{
				for (ssize_t xx{x1_min}; xx != x1_max + 1; ++xx)
				{
					const ssize_t index{(yy - 1) * (brush->m_W - 2) + (xx - 1)};
					terrainDeltas[index] += delta;
				}
			}
		}
	}

	// Move each vertex towards the computed average of its neighbours
	for (ssize_t dy{1}; dy != brush->m_H - 1; ++dy)
	{
		for (ssize_t dx{1}; dx != brush->m_W - 1; ++dx)
		{
			const float b{brush->Get(dx, dy)};
			if (!b)
				continue;

			const ssize_t index{(dy - 1) * (brush->m_W - 2) + (dx - 1)};
			this->MoveVertexTowards(x0 + dx, y0 + dy, static_cast<int>(terrainDeltas[index]), static_cast<int>(amount * b));
		}
	}
}

void HeightmapArray::FlattenElevationFromJs(const Brush* brush, float amount)
{
	int iAmount{static_cast<int>(amount)};

	ssize_t xc, yc;
	brush->GetCentre(xc, yc);
	const u16 height{this->GetVertex(xc, yc)};

	ssize_t x0, y0;
	brush->GetBottomLeft(x0, y0);

	for (ssize_t dy{0}; dy != brush->m_H; ++dy)
	{
		for (ssize_t dx{0}; dx != brush->m_W; ++dx)
		{
			const float b{brush->Get(dx, dy)};
			if (!b)
				continue;

			this->MoveVertexTowards(x0 + dx, y0 + dy, height, 1 + (int)(b * iAmount));
		}
	}
}
