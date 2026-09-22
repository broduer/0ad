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

#include "RenderingOptions.h"

#include "graphics/TextureManager.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/CStrIntern.h"
#include "ps/CStrInternStatic.h"
#include "ps/ConfigDB.h"
#include "ps/VideoMode.h"
#include "renderer/PostprocManager.h"
#include "renderer/Renderer.h"
#include "renderer/SceneRenderer.h"
#include "renderer/ShadowMap.h"
#include "renderer/WaterManager.h"
#include "renderer/backend/IDevice.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

CRenderingOptions g_RenderingOptions;
CRenderingOptions g_LogicRenderingOptions;

namespace
{

std::mutex g_PublishedMutex;
CRenderingOptions g_PublishedOptions;
bool g_OptionsPublished = false;
std::uint32_t g_PublishedGeneration = 0;

} // anonymous namespace

class CRenderingOptions::ConfigHooks
{
public:
	template<typename T>
	void Setup(CStr8 name, T& variable, CRenderingOptions& options)
	{
		hooks.emplace_back(g_ConfigDB.RegisterHookAndCall(name, [name, &variable, &options]() {
			variable = g_ConfigDB.Get(name, variable);
			++options.m_Generation;
		}));
	}
	void Setup(CStr8 name, std::function<void()> hook)
	{
		hooks.emplace_back(g_ConfigDB.RegisterHookAndCall(name, hook));
	}
	void clear() { hooks.clear(); }
private:
	std::vector<CConfigDBHook> hooks;
};

CRenderingOptions::ConfigHooks& CRenderingOptions::GetConfigHooks()
{
	static ConfigHooks hooks;
	return hooks;
}

RenderDebugMode RenderDebugModeEnum::FromString(const CStr8& name)
{
	if (name == str_RENDER_DEBUG_MODE_NONE.c_str())
		return RenderDebugMode::NONE;
	if (name == str_RENDER_DEBUG_MODE_AO.c_str())
		return RenderDebugMode::AO;
	if (name == str_RENDER_DEBUG_MODE_ALPHA.c_str())
		return RenderDebugMode::ALPHA;
	if (name == str_RENDER_DEBUG_MODE_CUSTOM.c_str())
		return RenderDebugMode::CUSTOM;

	LOGWARNING("Unknown render debug mode %s", name.c_str());
	return RenderDebugMode::NONE;
}

CStrIntern RenderDebugModeEnum::ToString(RenderDebugMode mode)
{
	switch (mode)
	{
	case RenderDebugMode::AO:
		return str_RENDER_DEBUG_MODE_AO;
	case RenderDebugMode::ALPHA:
		return str_RENDER_DEBUG_MODE_ALPHA;
	case RenderDebugMode::CUSTOM:
		return str_RENDER_DEBUG_MODE_CUSTOM;
	default:
		break;
	}
	return str_RENDER_DEBUG_MODE_NONE;
}

CRenderingOptions::CRenderingOptions()
{
	m_Shadows = false;
	m_WaterEffects = false;
	m_WaterFancyEffects = false;
	m_WaterRealDepth = false;
	m_WaterRefraction = false;
	m_WaterReflection = false;
	m_ShadowAlphaFix = false;
	m_ShadowPCF = false;
	m_Particles = false;
	m_Silhouettes = false;
	m_Fog = false;
	m_GPUSkinning = false;
	m_SmoothLOS = false;
	m_PostProc = false;
	m_DisplayFrustum = false;
	m_DisplayShadowsFrustum = false;
	m_RenderActors = true;
	m_CutsceneMode = false;
}

CRenderingOptions::~CRenderingOptions() = default;

void CRenderingOptions::ReadConfigAndSetupHooks()
{
	ConfigHooks& hooks{GetConfigHooks()};
	const auto effectHook = [this](const std::uint32_t effects) {
		return [this, effects]() { AddEffects(effects); };
	};

	hooks.Setup("shadowquality", effectHook(SHADOW_TEXTURE));
	hooks.Setup("shadowscascadecount", effectHook(SHADOW_TEXTURE | SHADERS));
	hooks.Setup("shadowscovermap", effectHook(SHADOW_TEXTURE | SHADERS));
	hooks.Setup("shadowscutoffdistance", effectHook(SHADOW_TEXTURE));

	hooks.Setup("shadows", [this]() {
		SetShadows(g_ConfigDB.Get("shadows", false));
	});
	hooks.Setup("shadowpcf", [this]() {
		SetShadowPCF(g_ConfigDB.Get("shadowpcf", false));
	});

	hooks.Setup("postproc", m_PostProc, *this);

	hooks.Setup("antialiasing", effectHook(ANTIALIASING));
	hooks.Setup("sharpness", effectHook(SHARPNESS));
	hooks.Setup("sharpening", effectHook(SHARPENING));
	hooks.Setup("renderer.scale", effectHook(POSTPROC_SCALE));
	hooks.Setup("renderer.upscale.technique", effectHook(UPSCALE));

	hooks.Setup("smoothlos", m_SmoothLOS, *this);

	hooks.Setup("watereffects", [this]() {
		SetWaterEffects(g_ConfigDB.Get("watereffects", false));
		AddEffects(WATER_TEXTURES);
	});
	hooks.Setup("waterfancyeffects", [this]() {
		SetWaterFancyEffects(g_ConfigDB.Get("waterfancyeffects", false));
		AddEffects(WATER_TEXTURES);
	});
	hooks.Setup("waterrealdepth", m_WaterRealDepth, *this);
	hooks.Setup("waterrefraction", [this]() {
		SetWaterRefraction(g_ConfigDB.Get("waterrefraction", false));
		AddEffects(WATER_TEXTURES);
	});
	hooks.Setup("waterreflection", [this]() {
		SetWaterReflection(g_ConfigDB.Get("waterreflection", false));
		AddEffects(WATER_TEXTURES);
	});

	hooks.Setup("particles", m_Particles, *this);
	hooks.Setup("fog", [this]() {
		SetFog(g_ConfigDB.Get("fog", false));
	});
	hooks.Setup("silhouettes", m_Silhouettes, *this);

	hooks.Setup("gpuskinning", [this]() {
		bool enabled{false};
		if (g_ConfigDB.Get("gpuskinning", false))
		{
			const Renderer::Backend::IDevice::Capabilities& capabilities{
				g_VideoMode.GetBackendDevice()->GetCapabilities()};
			if (capabilities.computeShaders && capabilities.storage)
				enabled = true;
			else
				LOGMESSAGE("GPU skinning isn't supported on the current hardware.");
		}
		SetGPUSkinning(enabled);
		AddEffects(SHADERS);
	});

	hooks.Setup("renderactors", m_RenderActors, *this);

	hooks.Setup("textures.quality", effectHook(TEXTURE_QUALITY));
	hooks.Setup("textures.maxanisotropy", effectHook(TEXTURE_QUALITY));

	hooks.Setup("pbr.brightness", m_PBRBrightness, *this);
}

void CRenderingOptions::ClearHooks()
{
	GetConfigHooks().clear();
}

void CRenderingOptions::AddEffects(const std::uint32_t effects)
{
	// A renderer created later starts from the current options.
	if (CRenderer::IsInitialised())
		m_Effects |= effects;
	++m_Generation;
}

void CRenderingOptions::SetShadows(bool value)
{
	m_Shadows = value;
	AddEffects(SHADERS);
}

void CRenderingOptions::SetShadowPCF(bool value)
{
	m_ShadowPCF = value;
	AddEffects(SHADERS);
}

void CRenderingOptions::SetFog(bool value)
{
	m_Fog = value;
	AddEffects(SHADERS);
}

void CRenderingOptions::SetRenderDebugMode(RenderDebugMode value)
{
	m_RenderDebugMode = value;
	AddEffects(SHADERS);
}


void PublishRenderingOptions()
{
	CRenderingOptions& options{g_LogicRenderingOptions};
	if (options.m_Generation == g_PublishedGeneration)
		return;
	g_PublishedGeneration = options.m_Generation;
	std::lock_guard<std::mutex> lock(g_PublishedMutex);
	const std::uint32_t effects{g_PublishedOptions.m_Effects | options.m_Effects};
	g_PublishedOptions = options;
	g_PublishedOptions.m_Effects = effects;
	g_OptionsPublished = true;
	options.m_Effects = 0;
}

void TakeRenderingOptions()
{
	CRenderingOptions& options{g_RenderingOptions};
	{
		std::lock_guard<std::mutex> lock(g_PublishedMutex);
		if (g_OptionsPublished)
		{
			// This also replaces fallbacks the render thread applied; the
			// effects apply them again.
			const std::uint32_t effects{options.m_Effects | g_PublishedOptions.m_Effects};
			options = g_PublishedOptions;
			options.m_Effects = effects;
			g_PublishedOptions.m_Effects = 0;
			g_OptionsPublished = false;
		}
	}
	if (options.m_Effects == 0 || !CRenderer::IsInitialised())
		return;
	const std::uint32_t effects{std::exchange(options.m_Effects, 0)};

	CSceneRenderer& sceneRenderer{g_Renderer.GetSceneRenderer()};
	if (effects & CRenderingOptions::SHADOW_TEXTURE)
		sceneRenderer.GetShadowMap().RecreateTexture();
	if (effects & CRenderingOptions::SHADERS)
		g_Renderer.MakeShadersDirty();
	CPostprocManager& postprocManager{g_Renderer.GetPostprocManager()};
	if (effects & CRenderingOptions::ANTIALIASING)
		postprocManager.UpdateAntiAliasingTechnique();
	if (effects & CRenderingOptions::SHARPNESS)
		postprocManager.UpdateSharpnessFactor();
	if (effects & CRenderingOptions::SHARPENING)
		postprocManager.UpdateSharpeningTechnique();
	if (effects & CRenderingOptions::POSTPROC_SCALE)
		postprocManager.Recreate();
	if (effects & CRenderingOptions::UPSCALE)
		postprocManager.SetUpscaleTechnique(g_ConfigDB.Get("renderer.upscale.technique", std::string{}));
	if (effects & CRenderingOptions::WATER_TEXTURES)
		sceneRenderer.GetWaterManager().RecreateOrLoadTexturesIfNeeded();
	if (effects & CRenderingOptions::TEXTURE_QUALITY)
		g_Renderer.GetTextureManager().OnQualityChanged();
}
