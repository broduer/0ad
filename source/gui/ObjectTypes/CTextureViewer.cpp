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

#include "CTextureViewer.h"

#include "gui/CGUI.h"
#include "graphics/Canvas2D.h"
#include "graphics/TerrainTextureEntry.h"
#include <graphics/TerrainTextureManager.h>

CTextureViewer::CTextureViewer(CGUI& pGUI)
	: IGUIObject(pGUI),
	m_TextureName(this, "texture_name")
{
}

void CTextureViewer::Draw(CCanvas2D& canvas)
{
	if (m_TextureName->m_Words.empty())
		return;

	CTerrainTextureEntry* tex{g_TexMan.FindTexture(m_TextureName->GetRawString().ToUTF8())};
	if (!tex)
		return;

	canvas.DrawTexture(tex->GetTexture(), GetActualSize());
}
