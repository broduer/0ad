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

#include "JSInterface_Renderer.h"

#include "graphics/TextureManager.h"
#include "ps/CStr.h"
#include "ps/CStrIntern.h"
#include "renderer/Renderer.h"
#include "renderer/RenderingOptions.h"
#include "scriptinterface/FunctionWrapper.h"

#include <string>

namespace JSI_Renderer
{
#define IMPLEMENT_BOOLEAN_SCRIPT_SETTING(NAME) \
bool Get##NAME##Enabled() \
{ \
	return g_RenderingOptions.Get##NAME(); \
} \
\
void Set##NAME##Enabled(bool enabled) \
{ \
	g_RenderingOptions.Set##NAME(enabled); \
}

IMPLEMENT_BOOLEAN_SCRIPT_SETTING(CutsceneMode);
IMPLEMENT_BOOLEAN_SCRIPT_SETTING(DisplayFrustum);
IMPLEMENT_BOOLEAN_SCRIPT_SETTING(DisplayShadowsFrustum);

#undef IMPLEMENT_BOOLEAN_SCRIPT_SETTING

std::string GetRenderPath()
{
	return RenderPathEnum::ToString(g_RenderingOptions.GetRenderPath());
}

std::string GetRenderDebugMode()
{
	return RenderDebugModeEnum::ToString(g_RenderingOptions.GetRenderDebugMode()).c_str();
}

void SetRenderDebugMode(const std::string& mode)
{
	g_RenderingOptions.SetRenderDebugMode(RenderDebugModeEnum::FromString(mode));
}

bool TextureExists(const std::wstring& filename)
{
	return g_Renderer.GetTextureManager().TextureExists(filename);
}

float GetPBRBrightness()
{
	return g_RenderingOptions.GetPBRBrightness();
}

void SetPBRBrightness(const float value)
{
	g_RenderingOptions.SetPBRBrightness(value);
}

#define REGISTER_BOOLEAN_SCRIPT_SETTING(NAME) \
ScriptFunction::Register<&Get##NAME##Enabled>(rq, "Renderer_Get" #NAME "Enabled"); \
ScriptFunction::Register<&Set##NAME##Enabled>(rq, "Renderer_Set" #NAME "Enabled");

void RegisterScriptFunctions(const Script::Request& rq)
{
	ScriptFunction::Register<&GetRenderPath>(rq, "Renderer_GetRenderPath");
	ScriptFunction::Register<&TextureExists>(rq, "TextureExists");
	ScriptFunction::Register<&GetRenderDebugMode>(rq, "Renderer_GetRenderDebugMode");
	ScriptFunction::Register<&SetRenderDebugMode>(rq, "Renderer_SetRenderDebugMode");
	ScriptFunction::Register<&GetPBRBrightness>(rq, "Renderer_GetPBRBrightness");
	ScriptFunction::Register<&SetPBRBrightness>(rq, "Renderer_SetPBRBrightness");
	REGISTER_BOOLEAN_SCRIPT_SETTING(CutsceneMode);
	REGISTER_BOOLEAN_SCRIPT_SETTING(DisplayFrustum);
	REGISTER_BOOLEAN_SCRIPT_SETTING(DisplayShadowsFrustum);
}

#undef REGISTER_BOOLEAN_SCRIPT_SETTING
}
