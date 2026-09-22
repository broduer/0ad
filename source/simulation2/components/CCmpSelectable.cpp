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

#include "ICmpSelectable.h"

#include "graphics/Color.h"
#include "graphics/Overlay.h"
#include "graphics/RenderWorld.h"
#include "lib/code_generation.h"
#include "lib/debug.h"
#include "lib/path.h"
#include "maths/FixedVector3D.h"
#include "ps/CLogger.h"
#include "ps/CStrIntern.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpFootprint.h"
#include "simulation2/components/ICmpOwnership.h"
#include "simulation2/components/ICmpPlayer.h"
#include "simulation2/components/ICmpPlayerManager.h"
#include "simulation2/components/ICmpPosition.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Message.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Minimum alpha value for always visible overlays [0 fully transparent, 1 fully opaque]
static const float MIN_ALPHA_ALWAYS_VISIBLE = 0.65f;
// Minimum alpha value for other overlays
static const float MIN_ALPHA_UNSELECTED = 0.0f;
// Desaturation value for unselected, always visible overlays (0.33 = 33% desaturated or 66% of original saturation)
static const float RGB_DESATURATION = 0.333333f;

class CCmpSelectable final : public ICmpSelectable
{
public:
	enum EShape
	{
		FOOTPRINT,
		CIRCLE,
		SQUARE
	};

	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_OwnershipChanged);
		componentManager.SubscribeToMessageType(MT_PlayerColorChanged);
		componentManager.SubscribeToMessageType(MT_PositionChanged);
		componentManager.SubscribeToMessageType(MT_TerrainChanged);
		componentManager.SubscribeToMessageType(MT_WaterChanged);
	}

	DEFAULT_COMPONENT_ALLOCATOR(Selectable)

	CCmpSelectable()
		: m_Selected(false), m_Cached(false), m_Visible(false)
	{
		m_Color = CColor(0, 0, 0, 0);
	}

	static std::string GetSchema()
	{
		return
			"<a:help>Allows this entity to be selected by the player.</a:help>"
			"<a:example/>"
			"<optional>"
				"<element name='EditorOnly' a:help='If this element is present, the entity is only selectable in Atlas'>"
					"<empty/>"
				"</element>"
			"</optional>"
			"<element name='Overlay' a:help='Specifies the type of overlay to be displayed when this entity is selected.'>"
				"<interleave>"
					"<optional>"
						"<element name='Shape' a:help='Specifies shape of overlay. If not specified, footprint shape will be used.'>"
							"<choice>"
								"<element name='Square' a:help='Set the overlay to a square of the given size'>"
									"<attribute name='width' a:help='Size of the overlay along the left/right direction (in meters)'>"
										"<data type='decimal'>"
											"<param name='minExclusive'>0.0</param>"
										"</data>"
									"</attribute>"
									"<attribute name='depth' a:help='Size of the overlay along the front/back direction (in meters)'>"
										"<data type='decimal'>"
											"<param name='minExclusive'>0.0</param>"
										"</data>"
									"</attribute>"
								"</element>"
								"<element name='Circle' a:help='Set the overlay to a circle of the given size'>"
									"<attribute name='radius' a:help='Radius of the overlay (in meters)'>"
										"<data type='decimal'>"
											"<param name='minExclusive'>0.0</param>"
										"</data>"
									"</attribute>"
								"</element>"
							"</choice>"
						"</element>"
					"</optional>"
					"<optional>"
						"<element name='AlwaysVisible' a:help='If this element is present, the selection overlay will always be visible (with transparency and desaturation)'>"
							"<empty/>"
						"</element>"
					"</optional>"
					"<choice>"
						"<element name='Texture' a:help='Displays a texture underneath the entity.'>"
							"<element name='MainTexture' a:help='Texture to display underneath the entity. Filepath relative to art/textures/selection/.'><text/></element>"
							"<element name='MainTextureMask' a:help='Mask texture that controls where to apply player color. Filepath relative to art/textures/selection/.'><text/></element>"
						"</element>"
						"<element name='Outline' a:help='Traces the outline of the entity with a line texture.'>"
							"<element name='LineTexture' a:help='Texture to apply to the line. Filepath relative to art/textures/selection/.'><text/></element>"
							"<element name='LineTextureMask' a:help='Texture that controls where to apply player color. Filepath relative to art/textures/selection/.'><text/></element>"
							"<element name='LineThickness' a:help='Thickness of the line, in world units.'><ref name='positiveDecimal'/></element>"
						"</element>"
					"</choice>"
				"</interleave>"
			"</element>";
	}

	EShape m_Shape;
	entity_pos_t m_Width; // width/radius
	entity_pos_t m_Height; // height/radius

	void Init(const CParamNode& paramNode) override
	{
		m_EditorOnly = paramNode.GetChild("EditorOnly").IsOk();

		// Certain special units always have their selection overlay shown
		m_AlwaysVisible = paramNode.GetChild("Overlay").GetChild("AlwaysVisible").IsOk();
		if (m_AlwaysVisible)
		{
			m_AlphaMin = MIN_ALPHA_ALWAYS_VISIBLE;
			m_TargetAlpha = m_AlphaMin;
			MarkRenderDirty();
		}
		else
			m_AlphaMin = MIN_ALPHA_UNSELECTED;

		const CParamNode& textureNode = paramNode.GetChild("Overlay").GetChild("Texture");
		const CParamNode& outlineNode = paramNode.GetChild("Overlay").GetChild("Outline");

		// Save some memory by using interned file paths in these descriptors (almost all actors and
		// entities have this component, and many use the same textures).
		if (textureNode.IsOk())
		{
			// textured quad mode (dynamic, for units)
			m_OverlayDescriptor.m_Type = DYNAMIC_QUAD;
			m_OverlayDescriptor.m_QuadTexture = CStrIntern(TEXTUREBASEPATH + textureNode.GetChild("MainTexture").ToString());
			m_OverlayDescriptor.m_QuadTextureMask = CStrIntern(TEXTUREBASEPATH + textureNode.GetChild("MainTextureMask").ToString());
		}
		else if (outlineNode.IsOk())
		{
			// textured outline mode (static, for buildings)
			m_OverlayDescriptor.m_Type = STATIC_OUTLINE;
			m_OverlayDescriptor.m_LineTexture = CStrIntern(TEXTUREBASEPATH + outlineNode.GetChild("LineTexture").ToString());
			m_OverlayDescriptor.m_LineTextureMask = CStrIntern(TEXTUREBASEPATH + outlineNode.GetChild("LineTextureMask").ToString());
			m_OverlayDescriptor.m_LineThickness = outlineNode.GetChild("LineThickness").ToFloat();
		}

		const CParamNode& shapeNode = paramNode.GetChild("Overlay").GetChild("Shape");
		if (shapeNode.IsOk())
		{
			if (shapeNode.GetChild("Square").IsOk())
			{
				m_Shape = SQUARE;
				m_Width = shapeNode.GetChild("Square").GetChild("@width").ToFixed();
				m_Height = shapeNode.GetChild("Square").GetChild("@depth").ToFixed();
			}
			else if (shapeNode.GetChild("Circle").IsOk())
			{
				m_Shape = CIRCLE;
				m_Width = m_Height = shapeNode.GetChild("Circle").GetChild("@radius").ToFixed();
			}
			else
			{
				// Should not happen
				m_Shape = FOOTPRINT;
				LOGWARNING("[Selectable] Selected overlay shape is not implemented.");
			}
		}
		else
		{
			m_Shape = FOOTPRINT;
		}
	}

	void Deinit() override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->overlays.emplace_back(RenderWorld::Overlay::RemoveSelection{GetEntityId()});
	}

	void Serialize(ISerializer&) override
	{
		// Nothing to do here (the overlay object is not worth saving, it'll get
		// reconstructed by the GUI soon enough, I think)
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer&) override
	{
		// Need to call Init to reload the template properties
		Init(paramNode);
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override;

	void SetSelectionHighlight(const CColor& color, bool selected) override
	{
		m_Selected = selected;
		m_Color.r = color.r;
		m_Color.g = color.g;
		m_Color.b = color.b;

		// Always-visible overlays will be desaturated if their parent unit is deselected.
		if (m_AlwaysVisible && !selected)
		{
			float max;

			// Reduce saturation by one-third, the quick-and-dirty way.
			if (m_Color.r > m_Color.b)
				max = (m_Color.r > m_Color.g) ? m_Color.r : m_Color.g;
			else
				max = (m_Color.b > m_Color.g) ? m_Color.b : m_Color.g;

			m_Color.r += (max - m_Color.r) * RGB_DESATURATION;
			m_Color.g += (max - m_Color.g) * RGB_DESATURATION;
			m_Color.b += (max - m_Color.b) * RGB_DESATURATION;
		}

		SetSelectionHighlightAlpha(color.a);
	}

	void SetSelectionHighlightAlpha(float alpha) override
	{
		m_TargetAlpha = std::max(m_AlphaMin, alpha);
		if (m_Visible && m_TargetAlpha > 0)
			EnsureColorCached();
		MarkRenderDirty();
	}

	void SetVisibility(bool visible) override
	{
		if (visible == m_Visible)
			return;
		m_Visible = visible;
		if (m_Visible && m_TargetAlpha > 0)
			EnsureColorCached();
		MarkRenderDirty();
	}

	bool IsEditorOnly() const override
	{
		return m_EditorOnly;
	}

	/// Explicitly invalidates the static overlay.
	void InvalidateStaticOverlay();

	/**
	 * Set the color of the current owner.
	 */
	void UpdateColor() override;

	void MarkRenderDirty();
	void EnsureColorCached();
	void PublishRenderState();

private:
	SOverlayDescriptor m_OverlayDescriptor;

	// Whether the selectable will be rendered.
	bool m_Visible;
	// Whether the entity is only selectable in Atlas editor
	bool m_EditorOnly;
	// Whether the selection overlay is always visible
	bool m_AlwaysVisible;
	/// Whether the parent entity is selected (caches GUI's selection state).
	bool m_Selected;
	/// Current selection overlay color. Its alpha is unused; see m_TargetAlpha.
	CColor m_Color;
	/// Alpha the overlay fades to. The render world starts a fade when it changes.
	float m_TargetAlpha = 0.f;
	/// Whether the selectable's player color has been cached for rendering.
	bool m_Cached;
	/// Minimum value for current selection overlay alpha.
	float m_AlphaMin;
	bool m_StaticOverlayStale = false;
	bool m_RenderDirty = false;

	static const char* TEXTUREBASEPATH;
};

const char* CCmpSelectable::TEXTUREBASEPATH = "art/textures/selection/";

void CCmpSelectable::HandleMessage(const CMessage& msg, bool /*global*/)
{
	switch (msg.GetType())
	{
	case MT_FrameStep:
	{
		PublishRenderState();
		break;
	}
	case MT_OwnershipChanged:
	{
		const CMessageOwnershipChanged& msgData = static_cast<const CMessageOwnershipChanged&> (msg);

		// Ignore newly constructed entities, as they receive their color upon first selection
		// Ignore deleted entities because they won't be rendered
		if (msgData.from == INVALID_PLAYER || msgData.to == INVALID_PLAYER)
			break;

		UpdateColor();
		InvalidateStaticOverlay();
		break;
	}
	case MT_PlayerColorChanged:
	{
		const CMessagePlayerColorChanged& msgData = static_cast<const CMessagePlayerColorChanged&> (msg);

		CmpPtr<ICmpOwnership> cmpOwnership(GetEntityHandle());
		if (!cmpOwnership || msgData.player != cmpOwnership->GetOwner())
			break;

		UpdateColor();
		break;
	}
	case MT_PositionChanged:
	case MT_TerrainChanged:
	case MT_WaterChanged:
		InvalidateStaticOverlay();
		break;
	}
}

void CCmpSelectable::UpdateColor()
{
	CmpPtr<ICmpOwnership> cmpOwnership(GetEntityHandle());

	CmpPtr<ICmpPlayerManager> cmpPlayerManager(GetSystemEntity());
	if (!cmpPlayerManager)
		return;

	// Default to white if there's no owner (e.g. decorative, editor-only actors)
	CColor color(1.0, 1.0, 1.0, 1.0);

	if (cmpOwnership)
	{
		CmpPtr<ICmpPlayer> cmpPlayer(GetSimContext(), cmpPlayerManager->GetPlayerByID(cmpOwnership->GetOwner()));
		if (cmpPlayer)
			color = cmpPlayer->GetDisplayedColor();
	}

	color.a = m_TargetAlpha;
	m_Cached = true;
	SetSelectionHighlight(color, m_Selected);
}

void CCmpSelectable::EnsureColorCached()
{
	if (!m_Cached)
		UpdateColor();
}

void CCmpSelectable::MarkRenderDirty()
{
	if (m_RenderDirty)
		return;
	m_RenderDirty = true;
	GetSimContext().GetComponentManager().RequestFrameStep(this);
}

void CCmpSelectable::PublishRenderState()
{
	m_RenderDirty = false;
	RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket();
	if (!packet)
		return;
	RenderWorld::SSelection selection;
	selection.entity = GetEntityId();
	selection.descriptor = m_OverlayDescriptor;
	selection.color = m_Color;
	selection.targetAlpha = m_TargetAlpha;
	selection.visible = m_Visible;
	selection.staticOverlayStale = m_StaticOverlayStale;
	m_StaticOverlayStale = false;

	CmpPtr<ICmpPosition> cmpPosition(GetEntityHandle());
	if (cmpPosition)
		selection.rotation = cmpPosition->GetRotation();
	selection.shapeKnown = m_Shape != FOOTPRINT;
	selection.square = m_Shape == SQUARE;
	if (m_Shape == FOOTPRINT)
	{
		CmpPtr<ICmpFootprint> cmpFootprint(GetEntityHandle());
		if (cmpFootprint)
		{
			ICmpFootprint::EShape shape;
			entity_pos_t h; // spurious
			cmpFootprint->GetShape(shape, m_Width, m_Height, h);
			selection.shapeKnown = true;
			selection.square = shape == ICmpFootprint::SQUARE;
		}
	}
	selection.width = m_Width.ToFloat();
	selection.height = m_Height.ToFloat();
	packet->overlays.emplace_back(RenderWorld::Overlay::SetSelection{std::move(selection)});
}

void CCmpSelectable::InvalidateStaticOverlay()
{
	m_StaticOverlayStale = true;
	MarkRenderDirty();
}

REGISTER_COMPONENT_TYPE(Selectable)
