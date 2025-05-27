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

#ifndef INCLUDED_CTEXTURE
#define INCLUDED_CTEXTURE

#include "gui/ObjectBases/IGUIObject.h"
#include "gui/SettingTypes/CGUIString.h"

class CTextureViewer : public IGUIObject
{
	GUI_OBJECT(CTextureViewer)

public:
	CTextureViewer(CGUI& pGUI);
	~CTextureViewer() = default;
	NONCOPYABLE(CTextureViewer);

protected:
	virtual void Draw(CCanvas2D& canvas);
	CGUISimpleSetting<CGUIString> m_TextureName;
};

#endif // INCLUDED_CTEXTURE
