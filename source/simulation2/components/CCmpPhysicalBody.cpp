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

#include "simulation2/components/ICmpPhysicalBody.h"

#include "ps/CLogger.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpFootprint.h"
#include "simulation2/components/ICmpUnitMotion.h"
#include "simulation2/helpers/PushConfig.h"
#include "simulation2/system/Component.h"

/**
 * A unit's physical body, see ICmpPhysicalBody.
 * Heavier units push harder and resist being pushed; pressure records how crowded it is.
 */
class CCmpPhysicalBody final : public ICmpPhysicalBody
{
public:
	static void ClassInit(CComponentManager& componentManager);
	DEFAULT_COMPONENT_ALLOCATOR(PhysicalBody);

	void Init(const CParamNode& paramNode) override;
	void Deinit() override;
	void Serialize(ISerializer& serialize) override;
	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override;
	void HandleMessage(const CMessage& msg, bool global) override;

	static std::string GetSchema()
	{
		return
			"<a:help>Physical properties of a unit: weight, roll limit and participation in pushing.</a:help>"
			"<optional>"
				"<element name='Weight' a:help='Makes this unit both push harder and harder to push. 10 is considered the base value.'>"
					"<ref name='positiveDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='MaxRollAngle' a:help='Maximum roll angle in radians when pushed (0 = no roll). Useful for ships and boats.'>"
					"<ref name='nonNegativeDecimal'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='DisableForces' a:help='Exclude this unit from pushing.'>"
					"<data type='boolean'/>"
				"</element>"
			"</optional>"
			"<optional>"
				"<element name='Solid' a:help='A solid unit collides as its Footprint outline: it pushes and is pushed along its length, turns when struck off its centre, is stopped rigidly by what it runs into, is held off other solid units and off walls rather than pushed into them, and never squashes into a crowd. Everything else collides as a circle of its pathfinding clearance, which crowds squash together, and which the pushing distances in pathfinder.xml are tuned for. Costlier to push: every pair a solid unit is in measures segment distances. Meant for ships and melee elephants.'>"
					"<data type='boolean'/>"
				"</element>"
			"</optional>";
	}

	// ICmpPhysicalBody interface implementation
	void SetEnabled(bool enabled) override;

	// Per-unit property accessors
	entity_pos_t GetWeight() const override;

	UnitCollision::CollisionShape GetCollisionShape() const override;

	bool IsPushingEnabled() const override;

	uint8_t GetPressure() const override;
	void SetPressure(uint8_t pressure) override;
	void AddPressure(uint8_t pressure) override;

	// Roll angle support
	fixed GetMaxRollAngle() const override;

private:
	// Template state (from XML)
	bool m_TemplatePushingEnabled;
	bool m_Solid;

	// Global enable state
	bool m_GlobalEnabled;

	// Per-unit runtime state
	entity_pos_t m_Weight;           // Mass/inertia - heavier units push harder
	bool m_PushingEnabled;           // Whether this unit takes part in pushing
	uint8_t m_Pressure;              // Current pressure (crowding intensity) 0-255
	fixed m_MaxRollAngle;            // Maximum roll angle for this unit (0 = no roll)

	// Derived from the Footprint or UnitMotion component, which may not exist yet when we are
	// initialised, so this is filled in on the first call to GetCollisionShape. It is a pure function of the
	// template and must not be serialized: both sides of a network game recompute the same value.
	mutable UnitCollision::CollisionShape m_CollisionShape;
	mutable bool m_CollisionShapeKnown;
};

REGISTER_COMPONENT_TYPE(PhysicalBody)

void CCmpPhysicalBody::ClassInit(CComponentManager& componentManager)
{
	componentManager.SubscribeToMessageType(MT_ValueModification);
}

void CCmpPhysicalBody::Init(const CParamNode& paramNode)
{
	// Load template values
	m_Weight = paramNode.GetChild("Weight").ToFixed();

	// Load MaxRollAngle from template
	if (paramNode.GetChild("MaxRollAngle").IsOk())
		m_MaxRollAngle = paramNode.GetChild("MaxRollAngle").ToFixed();
	else
		m_MaxRollAngle = fixed::Zero();

	m_TemplatePushingEnabled = !paramNode.GetChild("DisableForces").IsOk() ||
							 !paramNode.GetChild("DisableForces").ToBool();

	m_Solid = paramNode.GetChild("Solid").IsOk() && paramNode.GetChild("Solid").ToBool();

	// Check global force system is enabled
	PushConfig::GetInstance().EnsureLoaded();
	m_GlobalEnabled = PushConfig::GetInstance().IsEnabled();
	m_PushingEnabled = m_TemplatePushingEnabled && m_GlobalEnabled;

	// Initialize runtime state
	m_Pressure = 0;
	m_CollisionShapeKnown = false;

	// Ensure weight is positive
	if (m_Weight <= entity_pos_t::Zero())
	{
		LOGWARNING("PhysicalBody: Weight must be positive for entity %u, using default 10", GetEntityId());
		m_Weight = entity_pos_t::FromInt(10);
	}
}

void CCmpPhysicalBody::Deinit()
{
	// Nothing to clean up
}

void CCmpPhysicalBody::Serialize(ISerializer& serialize)
{
	// Serialize runtime state that needs to persist across saves
	serialize.NumberFixed_Unbounded("weight", m_Weight);
	serialize.Bool("pushingEnabled", m_PushingEnabled);
	serialize.NumberU8_Unbounded("pressure", m_Pressure);
	serialize.Bool("globalEnabled", m_GlobalEnabled);
}

void CCmpPhysicalBody::Deserialize(const CParamNode& paramNode, IDeserializer& deserialize)
{
	// First initialize with template values
	Init(paramNode);

	// Then restore runtime state
	entity_pos_t weight;
	deserialize.NumberFixed_Unbounded("weight", weight);
	m_Weight = weight;

	deserialize.Bool("pushingEnabled", m_PushingEnabled);

	uint8_t pressure;
	deserialize.NumberU8_Unbounded("pressure", pressure);
	m_Pressure = pressure;

	bool globalEnabled;
	deserialize.Bool("globalEnabled", globalEnabled);
	m_GlobalEnabled = globalEnabled;

}

void CCmpPhysicalBody::HandleMessage(const CMessage& msg, bool /*global*/)
{
	switch (msg.GetType())
	{
	case MT_ValueModification:
	{
		const CMessageValueModification& msgData = static_cast<const CMessageValueModification&>(msg);
		if (msgData.component == L"PhysicalBody")
		{
			// Re-evaluate force-enabled status (global setting may have changed)
			m_GlobalEnabled = PushConfig::GetInstance().IsEnabled();
			m_PushingEnabled = m_TemplatePushingEnabled && m_GlobalEnabled;
		}
		break;
	}
	default:
		break;
	}
}

void CCmpPhysicalBody::SetEnabled(bool enabled)
{
	m_GlobalEnabled = enabled;
	m_PushingEnabled = m_TemplatePushingEnabled && m_GlobalEnabled;
}

entity_pos_t CCmpPhysicalBody::GetWeight() const
{
	return m_Weight;
}

UnitCollision::CollisionShape CCmpPhysicalBody::GetCollisionShape() const
{
	if (m_CollisionShapeKnown)
		return m_CollisionShape;

	// A solid unit's body is its footprint, and anything else's is the circle of its pathfinding
	// clearance.
	CmpPtr<ICmpFootprint> cmpFootprint(GetEntityHandle());
	if (m_Solid && cmpFootprint)
	{
		ICmpFootprint::EShape shape;
		entity_pos_t size0, size1, height;
		cmpFootprint->GetShape(shape, size0, size1, height);
		m_CollisionShape = shape == ICmpFootprint::SQUARE ?
			UnitCollision::DeriveCollisionShape(size0, size1) :
			UnitCollision::DeriveCollisionShape(size0 * 2, size0 * 2);
	}
	else
	{
		// Without UnitMotion the entity isn't pushed at all, and has no "body". Don't
		// cache that: the component may simply not have been created yet.
		CmpPtr<ICmpUnitMotion> cmpUnitMotion(GetEntityHandle());
		if (!cmpUnitMotion)
			return UnitCollision::CollisionShape{entity_pos_t::Zero(), entity_pos_t::Zero(), false, false};
		m_CollisionShape = UnitCollision::CollisionShape{cmpUnitMotion->GetUnitClearance(), entity_pos_t::Zero(), false, false};
	}

	m_CollisionShapeKnown = true;
	return m_CollisionShape;
}

bool CCmpPhysicalBody::IsPushingEnabled() const
{
	return m_PushingEnabled;
}

uint8_t CCmpPhysicalBody::GetPressure() const
{
	return m_Pressure;
}

void CCmpPhysicalBody::SetPressure(uint8_t pressure)
{
	m_Pressure = std::min<uint8_t>(PushConfig::MAX_PRESSURE, pressure);
}

void CCmpPhysicalBody::AddPressure(uint8_t pressure)
{
	uint16_t newPressure = static_cast<uint16_t>(m_Pressure) + pressure;
	m_Pressure = static_cast<uint8_t>(std::min<uint16_t>(PushConfig::MAX_PRESSURE, newPressure));
}

fixed CCmpPhysicalBody::GetMaxRollAngle() const
{
    return m_MaxRollAngle;
}
