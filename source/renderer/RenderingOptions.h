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

/**
 * Keeps track of the settings used for rendering.
 * Ideally this header file should remain very quick to parse,
 * so avoid including other headers here unless absolutely necessary.
 *
 * g_LogicRenderingOptions belongs to the logic thread, where the config
 * hooks, scripts and cinematics write it, and g_RenderingOptions to the render
 * thread. The render thread takes changed options at the start of a frame and
 * applies their side effects.
 *
 * Lifetime concerns: both instances always exist, but hooks are tied to the configDB's lifetime
 * an the renderer may or may not actually exist.
 */

#ifndef INCLUDED_RENDERINGOPTIONS
#define INCLUDED_RENDERINGOPTIONS

#include "ps/CStr.h"

#include <cstdint>

class CStrIntern;

enum class RenderDebugMode
{
	NONE,
	AO,
	ALPHA,
	CUSTOM
};

struct RenderDebugModeEnum
{
	static RenderDebugMode FromString(const CStr8& name);
	static CStrIntern ToString(RenderDebugMode mode);
};

class CRenderingOptions
{
	// The renderer needs access to our private variables directly because capabilities have not yet been extracted
	// and thus sometimes it needs to change the rendering options without the side-effects.
	friend class CRenderer;

public:
	CRenderingOptions();
	~CRenderingOptions();

	// Logic instance only.
	void ReadConfigAndSetupHooks();
	void ClearHooks();

#define OPTION_DEFAULT_SETTER(NAME, TYPE) \
public: void Set##NAME(TYPE value) { m_##NAME = value; ++m_Generation; }\

#define OPTION_CUSTOM_SETTER(NAME, TYPE) \
public: void Set##NAME(TYPE value);\

#define OPTION_GETTER(NAME, TYPE)\
public: TYPE Get##NAME() const { return m_##NAME; }\

#define OPTION_DEF(NAME, TYPE)\
private: TYPE m_##NAME;

#define OPTION(NAME, TYPE)\
OPTION_DEFAULT_SETTER(NAME, TYPE); OPTION_GETTER(NAME, TYPE); OPTION_DEF(NAME, TYPE);

#define OPTION_WITH_SIDE_EFFECT(NAME, TYPE)\
OPTION_CUSTOM_SETTER(NAME, TYPE); OPTION_GETTER(NAME, TYPE); OPTION_DEF(NAME, TYPE);

	OPTION_WITH_SIDE_EFFECT(Shadows, bool);
	OPTION_WITH_SIDE_EFFECT(ShadowPCF, bool);
	OPTION_WITH_SIDE_EFFECT(Fog, bool);

	OPTION_WITH_SIDE_EFFECT(RenderDebugMode, RenderDebugMode);

	OPTION(WaterEffects, bool);
	OPTION(WaterFancyEffects, bool);
	OPTION(WaterRealDepth, bool);
	OPTION(WaterRefraction, bool);
	OPTION(WaterReflection, bool);

	OPTION(ShadowAlphaFix, bool);
	OPTION(Particles, bool);
	OPTION(GPUSkinning, bool);
	OPTION(Silhouettes, bool);
	OPTION(SmoothLOS, bool);
	OPTION(PostProc, bool);
	OPTION(DisplayFrustum, bool);
	OPTION(DisplayShadowsFrustum, bool);

	OPTION(PBRBrightness, float);

	// Cutscene Mode: while active, (most) visual overlays aren't rendered in order to give the scene a more "pure"
	// and real feel, like a movie.
	// The idea is for it to be inactive during normal gameplay, but enabled while playing cinema paths and
	// potentially during other scripted story events/cutscenes as well, like speech/dialogue.
	OPTION(CutsceneMode, bool);

	OPTION(RenderActors, bool);

#undef OPTION_DEFAULT_SETTER
#undef OPTION_CUSTOM_SETTER
#undef OPTION_GETTER
#undef OPTION_DEF
#undef OPTION
#undef OPTION_WITH_SIDE_EFFECT

private:
	friend void PublishRenderingOptions();
	friend void TakeRenderingOptions();
	class ConfigHooks;
	static ConfigHooks& GetConfigHooks();

	// What the render thread must redo when it takes the options.
	enum Effect : std::uint32_t
	{
		SHADOW_TEXTURE = 1 << 0,
		SHADERS = 1 << 1,
		ANTIALIASING = 1 << 2,
		SHARPNESS = 1 << 3,
		SHARPENING = 1 << 4,
		POSTPROC_SCALE = 1 << 5,
		UPSCALE = 1 << 6,
		WATER_TEXTURES = 1 << 7,
		TEXTURE_QUALITY = 1 << 8
	};
	void AddEffects(std::uint32_t effects);

	std::uint32_t m_Effects = 0;
	// Counts changes, so unchanged options are not published.
	std::uint32_t m_Generation = 0;
};

extern CRenderingOptions g_RenderingOptions;
extern CRenderingOptions g_LogicRenderingOptions;

/**
 * Hand changed options to the render thread. Logic thread.
 */
void PublishRenderingOptions();

/**
 * Take the latest published options into g_RenderingOptions and apply the
 * side effects of every change since the last call. Render thread.
 */
void TakeRenderingOptions();

#endif // INCLUDED_RENDERINGOPTIONS
