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

#include "ICmpVisual.h"

#include "graphics/Color.h"
#include "graphics/FramePacket.h"
#include "graphics/Model.h"
#include "graphics/ModelAbstract.h"
#include "graphics/RenderWorld.h"
#include "lib/debug.h"
#include "lib/path.h"
#include "lib/utf8.h"
#include "maths/BoundingBoxAligned.h"
#include "maths/BoundingBoxOriented.h"
#include "maths/Fixed.h"
#include "maths/FixedVector3D.h"
#include "maths/Vector3D.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/algorithm.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpFootprint.h"
#include "simulation2/components/ICmpIdentity.h"
#include "simulation2/components/ICmpMirage.h"
#include "simulation2/components/ICmpOwnership.h"
#include "simulation2/components/ICmpSound.h"
#include "simulation2/components/ICmpTemplateManager.h"
#include "simulation2/components/ICmpUnitRenderer.h"
#include "simulation2/components/ICmpValueModificationManager.h"
#include "simulation2/components/ICmpVisibility.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/serialization/SerializeTemplates.h"
#include "simulation2/serialization/SerializedTypes.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Interface.h"
#include "simulation2/system/Message.h"
#include "simulation2/system/SimContext.h"

#include <cstddef>
#include <js/Value.h>
#include <map>
#include <optional>
#include <string>
#include <utility>

class CCmpVisualActor final : public ICmpVisual
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_InterpolatedPositionChanged);
		componentManager.SubscribeToMessageType(MT_OwnershipChanged);
		componentManager.SubscribeToMessageType(MT_ValueModification);
		componentManager.SubscribeToMessageType(MT_Create);
		componentManager.SubscribeToMessageType(MT_Destroy);
	}

	DEFAULT_COMPONENT_ALLOCATOR(VisualActor)

private:
	std::wstring m_BaseActorName, m_ActorName;
	bool m_IsFoundationActor;

	// Whether the render world was told to build the unit, which happens only while displayed.
	bool m_HasUnit;
	player_id_t m_PlayerID = INVALID_PLAYER;
	std::optional<CModelAbstract::CustomSelectionShape> m_SelectionShape;

	fixed m_R, m_G, m_B; // shading color

	// Current animation state
	std::string m_AnimName;
	bool m_AnimOnce;
	fixed m_AnimSpeed;
	std::wstring m_SoundGroup;
	fixed m_AnimDesync;
	fixed m_AnimSyncRepeatTime; // 0.0 if not synced
	fixed m_AnimSyncOffsetTime;

	std::map<CStr, CStr> m_VariantSelections;

	std::uint32_t m_Seed; // seed used for random variations

	bool m_ConstructionPreview;

	bool m_VisibleInAtlasOnly;
	bool m_IsActorOnly;	// an in-world entity should not have this or it might not be rendered.

	bool m_SilhouetteDisplay;
	bool m_SilhouetteOccluder;
	bool m_ShadowsCast;
	bool m_ShadowsReceive;

	bool m_RegisteredWithUnitRenderer;

	/**
	 * Add a record for the render world, once it has the entity's unit.
	 */
	template<typename R>
	void Record(R&& record)
	{
		if (!m_HasUnit)
			return;
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->visuals.emplace_back(std::forward<R>(record));
	}

	void SetPlayerID(player_id_t playerID)
	{
		m_PlayerID = playerID;
		Record(RenderWorld::Visual::SetPlayer{GetEntityId(), playerID});
	}

	RenderWorld::SAnimation DescribeAnimation() const
	{
		return {m_AnimName, m_AnimOnce, m_AnimSpeed.ToFloat(), m_AnimDesync.ToFloat(), m_SoundGroup};
	}

	/**
	 * Describe everything the render world needs to build and keep the unit.
	 */
	RenderWorld::SVisual DescribeVisual() const;

	const RenderWorld::SUnitFeedback* GetFeedback() const
	{
		const RenderWorld::SFeedback* feedback = GetSimContext().GetRenderFeedback();
		return feedback ? feedback->Find(GetEntityId()) : nullptr;
	}

public:
	static std::string GetSchema()
	{
		return
			"<a:help>Display the unit using the engine's actor system.</a:help>"
			"<a:example>"
				"<Actor>units/hellenes/infantry_spearman_b.xml</Actor>"
			"</a:example>"
			"<a:example>"
				"<Actor>structures/hellenes/barracks.xml</Actor>"
				"<FoundationActor>structures/fndn_4x4.xml</FoundationActor>"
			"</a:example>"
			"<element name='Actor' a:help='Filename of the actor to be used for this unit'>"
				"<text/>"
			"</element>"
			"<optional>"
				"<element name='FoundationActor' a:help='Filename of the actor to be used the foundation while this unit is being constructed'>"
					"<text/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='Foundation' a:help='Used internally; if present, the unit will be rendered as a foundation'>"
					"<empty/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='ConstructionPreview' a:help='If present, the unit should have a construction preview'>"
					"<empty/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='ActorOnly' a:help='Used internally; if present, the unit will only be rendered if the user has high enough graphical settings.'>"
					"<empty/>"
				"</element>"
			"</optional>"
			"<element name='SilhouetteDisplay'>"
				"<data type='boolean'/>"
			"</element>"
			"<element name='SilhouetteOccluder'>"
				"<data type='boolean'/>"
			"</element>"
			"<optional>"
				"<element name='SelectionShape'>"
					"<choice>"
						"<element name='Bounds' a:help='Determines the selection box based on the model bounds'>"
							"<empty/>"
						"</element>"
						"<element name='Footprint' a:help='Determines the selection box based on the entity Footprint component'>"
							"<empty/>"
						"</element>"
						"<element name='Box' a:help='Sets the selection shape to a box of specified dimensions'>"
							"<attribute name='width'>"
								"<data type='decimal'>"
									"<param name='minExclusive'>0.0</param>"
								"</data>"
							"</attribute>"
							"<attribute name='height'>"
								"<data type='decimal'>"
									"<param name='minExclusive'>0.0</param>"
								"</data>"
							"</attribute>"
							"<attribute name='depth'>"
								"<data type='decimal'>"
									"<param name='minExclusive'>0.0</param>"
								"</data>"
							"</attribute>"
						"</element>"
						"<element name='Cylinder' a:help='Sets the selection shape to a cylinder of specified dimensions'>"
							"<attribute name='radius'>"
								"<data type='decimal'>"
									"<param name='minExclusive'>0.0</param>"
								"</data>"
							"</attribute>"
							"<attribute name='height'>"
								"<data type='decimal'>"
									"<param name='minExclusive'>0.0</param>"
								"</data>"
							"</attribute>"
						"</element>"
					"</choice>"
				"</element>"
			"</optional>"
			"<element name='VisibleInAtlasOnly'>"
				"<data type='boolean'/>"
			"</element>"
			"<optional>"
				"<element name='ShadowsCast'>"
					"<a:help>If true (default), the entity will cast dynamic shadows onto the environment. "
					"Set to false to avoid unnecessary shadow computation for objects that do not need to affect scene lighting, "
					"such as transparent effects, previews, or decorative visuals.</a:help>"
					"<data type='boolean'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='ShadowsReceive'>"
					"<a:help>If true (default), the entity will receive dynamic shadows from other objects. "
					"Set to false if the object should appear fully lit regardless of surrounding shadows, which can be useful "
					"for performance tuning or effects that simulate glowing or volumetric visuals.</a:help>"
					"<data type='boolean'/>"
				"</element>"
			"</optional>";
	}

	void Init(const CParamNode& paramNode) override
	{
		m_HasUnit = false;
		m_RegisteredWithUnitRenderer = false;
		m_R = m_G = m_B = fixed::FromInt(1);

		m_ConstructionPreview = paramNode.GetChild("ConstructionPreview").IsOk();

		m_Seed = GetEntityId();

		m_IsFoundationActor = paramNode.GetChild("Foundation").IsOk() && paramNode.GetChild("FoundationActor").IsOk();

		m_BaseActorName = paramNode.GetChild(m_IsFoundationActor ? "FoundationActor" : "Actor").ToWString();
		ParseActorName(m_BaseActorName);

		m_VisibleInAtlasOnly = paramNode.GetChild("VisibleInAtlasOnly").ToBool();
		m_IsActorOnly = paramNode.GetChild("ActorOnly").IsOk();

		m_SilhouetteDisplay = paramNode.GetChild("SilhouetteDisplay").ToBool();
		m_SilhouetteOccluder = paramNode.GetChild("SilhouetteOccluder").ToBool();

		m_ShadowsCast = m_VisibleInAtlasOnly ? false : (paramNode.GetChild("ShadowsCast").IsOk() ? paramNode.GetChild("ShadowsCast").ToBool() : true);
		m_ShadowsReceive = m_VisibleInAtlasOnly ? false : (paramNode.GetChild("ShadowsReceive").IsOk() ? paramNode.GetChild("ShadowsReceive").ToBool() : true);

		// Initialize the model's selection shape descriptor. This currently relies on the component initialization order; the
		// Footprint component must be initialized before this component (VisualActor) to support the ability to use the footprint
		// shape for the selection box (instead of the default recursive bounding box). See TypeList.h for the order in
		// which components are initialized; if for whatever reason you need to get rid of this dependency, you can always just
		// initialize the selection shape descriptor on-demand.
		InitSelectionShapeDescriptor(paramNode);
	}

	void Deinit() override
	{
		if (m_HasUnit)
		{
			Record(RenderWorld::Visual::Destroy{GetEntityId()});
			m_HasUnit = false;
		}
	}

	template<typename S>
	void SerializeCommon(S& serialize)
	{
		serialize.NumberFixed_Unbounded("r", m_R);
		serialize.NumberFixed_Unbounded("g", m_G);
		serialize.NumberFixed_Unbounded("b", m_B);

		serialize.StringASCII("anim name", m_AnimName, 0, 256);
		serialize.Bool("anim once", m_AnimOnce);
		serialize.NumberFixed_Unbounded("anim speed", m_AnimSpeed);
		serialize.String("sound group", m_SoundGroup, 0, 256);
		serialize.NumberFixed_Unbounded("anim desync", m_AnimDesync);
		serialize.NumberFixed_Unbounded("anim sync repeat time", m_AnimSyncRepeatTime);
		serialize.NumberFixed_Unbounded("anim sync offset time", m_AnimSyncOffsetTime);

		Serializer(serialize, "variation", m_VariantSelections);

		serialize.NumberU32_Unbounded("seed", m_Seed);
		serialize.String("actor", m_ActorName, 0, 256);

		// TODO: store actor variables?
	}

	void Serialize(ISerializer& serialize) override
	{
		// TODO: store the actor name, if !debug and it differs from the template

		if (serialize.IsDebug())
		{
			serialize.String("base actor", m_BaseActorName, 0, 256);
		}

		SerializeCommon(serialize);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);

		std::uint32_t oldSeed = GetActorSeed();

		SerializeCommon(deserialize);

		InitModel();

		// If we serialized a different seed or different actor, reload actor
		if (oldSeed != GetActorSeed() || m_BaseActorName != m_ActorName)
			ReloadActor();
		else
			ReloadUnitAnimation();

		if (m_HasUnit)
		{
			CmpPtr<ICmpOwnership> cmpOwnership(GetEntityHandle());
			if (cmpOwnership)
				SetPlayerID(cmpOwnership->GetOwner());
		}
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_OwnershipChanged:
		{
			RecomputeActorName();

			if (!m_HasUnit)
				break;

			const CMessageOwnershipChanged& msgData = static_cast<const CMessageOwnershipChanged&> (msg);
			SetPlayerID(msgData.to);

			break;
		}
		case MT_ValueModification:
		{
			// Mirages don't respond to technology modifications.
			CmpPtr<ICmpMirage> cmpMirage(GetEntityHandle());
			if (cmpMirage)
				return;

			const CMessageValueModification& msgData = static_cast<const CMessageValueModification&> (msg);
			if (msgData.component != L"VisualActor")
				break;

			RecomputeActorName();

			break;
		}
		case MT_InterpolatedPositionChanged:
		{
			if (m_RegisteredWithUnitRenderer)
			{
				CmpPtr<ICmpUnitRenderer> cmpUnitRenderer(GetSystemEntity());
				cmpUnitRenderer->MarkVisibilityDirty(GetEntityId());
			}
			break;
		}
		case MT_Create:
		{
			InitModel();

			SelectAnimation("idle");
			break;
		}
		case MT_Destroy:
		{
			if (m_RegisteredWithUnitRenderer)
			{
				CmpPtr<ICmpUnitRenderer> cmpUnitRenderer(GetSystemEntity());
				cmpUnitRenderer->RemoveUnit(GetEntityId());
				m_RegisteredWithUnitRenderer = false;
			}
			break;
		}
		}
	}

	CBoundingBoxAligned GetBounds() const override
	{
		const RenderWorld::SUnitFeedback* feedback = GetFeedback();
		return feedback ? feedback->bounds : CBoundingBoxAligned::EMPTY;
	}

	CBoundingBoxOriented GetSelectionBox() const override
	{
		const RenderWorld::SUnitFeedback* feedback = GetFeedback();
		return feedback ? feedback->selectionBox : CBoundingBoxOriented::EMPTY;
	}

	CVector3D GetPosition() const override
	{
		const RenderWorld::SUnitFeedback* feedback = GetFeedback();
		return feedback ? feedback->position : CVector3D(0, 0, 0);
	}

	std::wstring GetProjectileActor() const override
	{
		const RenderWorld::SUnitFeedback* feedback = GetFeedback();
		return feedback ? GetSimContext().GetRenderFeedback()->GetProjectileActor(*feedback) : L"";
	}

	CFixedVector3D GetProjectileLaunchPoint() const override
	{
		const RenderWorld::SUnitFeedback* feedback = GetFeedback();
		if (!feedback || !feedback->hasLaunchPoint)
			return CFixedVector3D();
		const CVector3D& vector = feedback->launchPoint;
		return CFixedVector3D(fixed::FromFloat(vector.X), fixed::FromFloat(vector.Y), fixed::FromFloat(vector.Z));
	}

	void SetVariant(const CStr& key, const CStr& selection) override
	{
		if (m_VariantSelections[key] == selection)
			return;

		m_VariantSelections[key] = selection;

		Record(RenderWorld::Visual::SetSelection{GetEntityId(), key, selection});
	}

	std::string GetAnimationName() const override
	{
		return m_AnimName;
	}

	void SelectAnimation(const std::string& name, bool once = false, fixed speed = fixed::FromInt(1)) override
	{
		m_AnimName = name;
		m_AnimOnce = once;
		m_AnimSpeed = speed;
		m_SoundGroup = L"";
		m_AnimDesync = fixed::FromInt(1)/20; // TODO: make this an argument
		m_AnimSyncRepeatTime = fixed::Zero();
		m_AnimSyncOffsetTime = fixed::Zero();

		SetVariant("animation", m_AnimName);

		CmpPtr<ICmpSound> cmpSound(GetEntityHandle());
		if (cmpSound)
			m_SoundGroup = cmpSound->GetSoundGroup(wstring_from_utf8(m_AnimName));

		Record(RenderWorld::Visual::SetAnimation{GetEntityId(), DescribeAnimation()});
	}

	void SelectMovementAnimation(const std::string& name, fixed speed) override
	{
		ENSURE(name == "idle" || name == "walk" || name == "run");
		if (m_AnimName != "idle" && m_AnimName != "walk" && m_AnimName != "run")
			return;
		if (m_AnimName == name && speed == m_AnimSpeed)
			return;
		SelectAnimation(name, false, speed);
	}

	void SetAnimationSyncRepeat(fixed repeattime) override
	{
		m_AnimSyncRepeatTime = repeattime;

		Record(RenderWorld::Visual::SetSyncRepeat{GetEntityId(), m_AnimSyncRepeatTime.ToFloat()});
	}

	void SetAnimationSyncOffset(fixed actiontime) override
	{
		m_AnimSyncOffsetTime = actiontime;

		Record(RenderWorld::Visual::SetSyncOffset{GetEntityId(), m_AnimSyncOffsetTime.ToFloat()});
	}

	// TODO: Why is `a` even an argument?
	void SetShadingColor(fixed r, fixed g, fixed b, fixed /*a*/) override
	{
		m_R = r;
		m_G = g;
		m_B = b;

		Record(RenderWorld::Visual::SetShading{GetEntityId(), CColor(m_R.ToFloat(), m_G.ToFloat(), m_B.ToFloat(), 1.0f)});
	}

	void SetVariable(const std::string& name, float value) override
	{
		Record(RenderWorld::Visual::SetVariable{GetEntityId(), name, value});
	}

	std::uint32_t GetActorSeed() const override
	{
		return m_Seed;
	}

	void SetActorSeed(std::uint32_t seed) override
	{
		if (seed == m_Seed)
			return;

		m_Seed = seed;
		ReloadActor();
	}

	void RecomputeActorName() override
	{
		CmpPtr<ICmpValueModificationManager> cmpValueModificationManager(GetSystemEntity());
		std::wstring newActorName;
		if (m_IsFoundationActor)
			newActorName = cmpValueModificationManager->ApplyModifications(L"VisualActor/FoundationActor", m_BaseActorName, GetEntityId());
		else
			newActorName = cmpValueModificationManager->ApplyModifications(L"VisualActor/Actor", m_BaseActorName, GetEntityId());

		if (newActorName != m_ActorName)
		{
			ParseActorName(newActorName);
			ReloadActor();
		}
	}

	bool HasConstructionPreview() const override
	{
		return m_ConstructionPreview;
	}

private:
	// Replace {phenotype} and {civ} with the correct value in m_ActorName
	void ParseActorName(std::wstring base);

	/// Helper function shared by component init and actor reloading
	void InitModel();

	/// Helper method; initializes the model selection shape descriptor from XML. Factored out for readability of @ref Init.
	void InitSelectionShapeDescriptor(const CParamNode& paramNode);

	// ReloadActor is used when the actor or seed changes.
	void ReloadActor();
	// ReloadUnitAnimation is used for a minimal reloading upon deserialization, when the actor and seed are identical.
	// It is also used by ReloadActor.
	void ReloadUnitAnimation();
};

REGISTER_COMPONENT_TYPE(VisualActor)

// ------------------------------------------------------------------------------------------------------------------

void CCmpVisualActor::ParseActorName(std::wstring base)
{
	CmpPtr<ICmpIdentity> cmpIdentity(GetEntityHandle());
	if (cmpIdentity)
	{
		PS::ReplaceSubrange(base, L"{phenotype}", cmpIdentity->GetPhenotype());
		PS::ReplaceSubrange(base, L"{civ}", cmpIdentity->GetCiv());
	}

	m_ActorName = std::move(base);
}

RenderWorld::SVisual CCmpVisualActor::DescribeVisual() const
{
	RenderWorld::SVisual visual;
	visual.actorName = m_ActorName;
	visual.seed = GetActorSeed();
	visual.selections = m_VariantSelections;
	visual.selectionShape = m_SelectionShape;

	if (m_SilhouetteDisplay)
		visual.modelFlags |= ModelFlag::SILHOUETTE_DISPLAY;
	if (m_SilhouetteOccluder)
		visual.modelFlags |= ModelFlag::SILHOUETTE_OCCLUDER;
	CmpPtr<ICmpVisibility> cmpVisibility(GetEntityHandle());
	if (cmpVisibility && cmpVisibility->GetAlwaysVisible())
		visual.modelFlags |= ModelFlag::IGNORE_LOS;

	visual.shadowsCast = m_ShadowsCast;
	visual.shadowsReceive = m_ShadowsReceive;
	visual.actorOnly = m_IsActorOnly;
	visual.visibleInAtlasOnly = m_VisibleInAtlasOnly;
	visual.player = m_PlayerID;
	visual.shading = CColor(m_R.ToFloat(), m_G.ToFloat(), m_B.ToFloat(), 1.0f);
	visual.animation = DescribeAnimation();
	visual.syncRepeat = m_AnimSyncRepeatTime.ToFloat();
	visual.syncOffset = m_AnimSyncOffsetTime.ToFloat();
	return visual;
}

void CCmpVisualActor::InitModel()
{
	if (!GetSimContext().GetFramePacket())
		return;

	m_HasUnit = true;
	Record(RenderWorld::Visual::Create{GetEntityId(), DescribeVisual()});

	if (!m_RegisteredWithUnitRenderer)
	{
		CmpPtr<ICmpUnitRenderer> cmpUnitRenderer(GetSystemEntity());
		if (cmpUnitRenderer)
		{
			cmpUnitRenderer->AddUnit(GetEntityHandle());
			m_RegisteredWithUnitRenderer = true;
		}
	}
}

void CCmpVisualActor::InitSelectionShapeDescriptor(const CParamNode& paramNode)
{
	// by default, we don't need a custom selection shape and we can just keep the default behaviour
	m_SelectionShape.reset();

	const CParamNode& shapeNode = paramNode.GetChild("SelectionShape");
	if (shapeNode.IsOk())
	{
		if (shapeNode.GetChild("Bounds").IsOk())
		{
			// default; no need to take action
		}
		else if (shapeNode.GetChild("Footprint").IsOk())
		{
			CmpPtr<ICmpFootprint> cmpFootprint(GetEntityHandle());
			if (cmpFootprint)
			{
				ICmpFootprint::EShape fpShape;				// fp stands for "footprint"
				entity_pos_t fpSize0, fpSize1, fpHeight;	// fp stands for "footprint"
				cmpFootprint->GetShape(fpShape, fpSize0, fpSize1, fpHeight);

				float size0 = fpSize0.ToFloat();
				float size1 = fpSize1.ToFloat();

				// TODO: we should properly distinguish between CIRCLE and SQUARE footprint shapes here, but since cylinders
				// aren't implemented yet and are almost indistinguishable from boxes for small enough sizes anyway,
				// we'll just use boxes for either case. However, for circular footprints the size0 and size1 values both
				// represent the radius, so we do have to adjust them to match the size1 and size0's of square footprints
				// (which represent the full width and depth).
				if (fpShape == ICmpFootprint::CIRCLE)
				{
					size0 *= 2;
					size1 *= 2;
				}

				CModelAbstract::CustomSelectionShape& shape = m_SelectionShape.emplace();
				shape.m_Type = CModelAbstract::CustomSelectionShape::BOX;
				shape.m_Size0 = size0;
				shape.m_Size1 = size1;
				shape.m_Height = fpHeight.ToFloat();
			}
			else
			{
				LOGERROR("[VisualActor] Cannot apply footprint-based SelectionShape; Footprint component not initialized.");
			}
		}
		else if (shapeNode.GetChild("Box").IsOk())
		{
			// TODO: we might need to support the ability to specify a different box center in the future
			CModelAbstract::CustomSelectionShape& shape = m_SelectionShape.emplace();
			shape.m_Type = CModelAbstract::CustomSelectionShape::BOX;
			shape.m_Size0 = shapeNode.GetChild("Box").GetChild("@width").ToFixed().ToFloat();
			shape.m_Size1 = shapeNode.GetChild("Box").GetChild("@depth").ToFixed().ToFloat();
			shape.m_Height = shapeNode.GetChild("Box").GetChild("@height").ToFixed().ToFloat();
		}
		else if (shapeNode.GetChild("Cylinder").IsOk())
		{
			LOGWARNING("[VisualActor] TODO: Cylinder selection shapes are not yet implemented; defaulting to recursive bounding boxes");
		}
		else
		{
			// shouldn't happen by virtue of validation against schema
			LOGERROR("[VisualActor] No selection shape specified");
		}
	}
}

void CCmpVisualActor::ReloadActor()
{
	if (!m_HasUnit)
		return;

	// HACK: selection shape needs template data, but rather than storing all that data
	//	in the component, we load the template here and pass it into a helper function
	CmpPtr<ICmpTemplateManager> cmpTemplateManager(GetSystemEntity());
	const CParamNode* node = cmpTemplateManager->LoadLatestTemplate(GetEntityId());
	ENSURE(node && node->GetChild("VisualActor").IsOk());

	InitSelectionShapeDescriptor(node->GetChild("VisualActor"));

	Record(RenderWorld::Visual::Rebuild{GetEntityId(), DescribeVisual()});
}

void CCmpVisualActor::ReloadUnitAnimation()
{
	Record(RenderWorld::Visual::ReloadAnimation{GetEntityId(), DescribeVisual()});
}
