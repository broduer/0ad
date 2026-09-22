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

#include "ICmpPosition.h"

#include "graphics/FramePacket.h"
#include "maths/Fixed.h"
#include "maths/FixedVector2D.h"
#include "maths/FixedVector3D.h"
#include "maths/MathUtil.h"
#include "maths/Matrix3D.h"
#include "maths/Vector3D.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpTerrain.h"
#include "simulation2/components/ICmpWaterManager.h"
#include "simulation2/helpers/Position.h"
#include "simulation2/serialization/SerializeTemplates.h"
#include "simulation2/serialization/SerializedTypes.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Message.h"
#include "simulation2/system/SimContext.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <utility>

/**
 * Basic ICmpPosition implementation.
 */
class CCmpPosition final : public ICmpPosition
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_TurnStart);
		componentManager.SubscribeToMessageType(MT_TerrainChanged);
		componentManager.SubscribeToMessageType(MT_WaterChanged);
		componentManager.SubscribeToMessageType(MT_Deserialized);

		// TODO: if this component turns out to be a performance issue, it should
		// be optimised by creating a new PositionStatic component that doesn't subscribe
		// to messages and doesn't store LastX/LastZ, and that should be used for all
		// entities that don't move
	}

	DEFAULT_COMPONENT_ALLOCATOR(Position)

	// Template state:

	using AnchorType = RenderWorld::AnchorType;

	AnchorType m_AnchorType;

	bool m_Floating;
	entity_pos_t m_FloatDepth;

	// Maximum radians per second, used by InterpolatedRotY to follow RotY and the unitMotion.
	fixed m_RotYSpeed;

	// Dynamic state:

	bool m_InWorld;
	// m_LastX/Z contain the position from the start of the most recent turn
	// m_PrevX/Z conatain the position from the turn before that
	entity_pos_t m_X, m_Z, m_LastX, m_LastZ, m_PrevX, m_PrevZ; // these values contain undefined junk if !InWorld

	entity_pos_t m_Y, m_LastYDifference; // either the relative or the absolute Y coordinate
	bool m_RelativeToGround; // whether m_Y is relative to terrain/water plane, or an absolute height

	fixed m_ConstructionProgress;

	// when the entity is a turret, only m_RotY is used, and this is the rotation
	// relative to the parent entity
	entity_angle_t m_RotX, m_RotY, m_RotZ;

	entity_id_t m_TurretParent;
	CFixedVector3D m_TurretPosition;
	std::set<entity_id_t> m_Turrets;

	// Not serialized: the events since the last record for the render world.
	bool m_TurnStarted, m_TurnMoved, m_SnapRotY, m_SnapRotXZ;
	bool m_RenderDirty;

	static std::string GetSchema()
	{
		return
			"<a:help>Allows this entity to exist at a location (and orientation) in the world, and defines some details of the positioning.</a:help>"
			"<a:example>"
				"<Anchor>upright</Anchor>"
				"<Altitude>0.0</Altitude>"
				"<Floating>false</Floating>"
				"<FloatDepth>0.0</FloatDepth>"
				"<TurnRate>6.0</TurnRate>"
			"</a:example>"
			"<element name='Anchor' a:help='Automatic rotation to follow the slope of terrain'>"
				"<choice>"
					"<value a:help='Always stand straight up (e.g. humans)'>upright</value>"
					"<value a:help='Rotate backwards and forwards to follow the terrain (e.g. animals)'>pitch</value>"
					"<value a:help='Rotate sideways to follow the terrain'>roll</value>"
					"<value a:help='Rotate in all directions to follow the terrain (e.g. carts)'>pitch-roll</value>"
				"</choice>"
			"</element>"
			"<element name='Altitude' a:help='Height above terrain in meters'>"
				"<data type='decimal'/>"
			"</element>"
			"<element name='Floating' a:help='Whether the entity floats on water'>"
				"<data type='boolean'/>"
			"</element>"
			"<element name='FloatDepth' a:help='The depth at which an entity floats on water (needs Floating to be true)'>"
				"<ref name='nonNegativeDecimal'/>"
			"</element>"
			"<element name='TurnRate' a:help='Maximum rotation speed around Y axis, in radians per second. Used for all graphical rotations and some real unitMotion driven rotations.'>"
				"<ref name='positiveDecimal'/>"
			"</element>";
	}

	void Init(const CParamNode& paramNode) override
	{
		m_AnchorType = RenderWorld::ParseAnchorType(paramNode.GetChild("Anchor").ToString());

		m_InWorld = false;

		m_LastYDifference = entity_pos_t::Zero();
		m_Y = paramNode.GetChild("Altitude").ToFixed();
		m_RelativeToGround = true;
		m_Floating = paramNode.GetChild("Floating").ToBool();
		m_FloatDepth = paramNode.GetChild("FloatDepth").ToFixed();

		m_RotYSpeed = paramNode.GetChild("TurnRate").ToFixed();

		m_RotX = m_RotY = m_RotZ = entity_angle_t::FromInt(0);
		m_TurnStarted = m_TurnMoved = m_SnapRotY = m_SnapRotXZ = false;
		m_RenderDirty = false;
		MarkRenderDirty();

		m_TurretParent = INVALID_ENTITY;
		m_TurretPosition = CFixedVector3D();
	}

	void Deinit() override
	{
		if (RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket())
			packet->removedPositions.push_back(GetEntityId());
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.Bool("in world", m_InWorld);
		if (m_InWorld)
		{
			serialize.NumberFixed_Unbounded("x", m_X);
			serialize.NumberFixed_Unbounded("y", m_Y);
			serialize.NumberFixed_Unbounded("z", m_Z);
			serialize.NumberFixed_Unbounded("last x", m_LastX);
			serialize.NumberFixed_Unbounded("last y diff", m_LastYDifference);
			serialize.NumberFixed_Unbounded("last z", m_LastZ);
		}

		serialize.NumberFixed_Unbounded("rot x", m_RotX);
		serialize.NumberFixed_Unbounded("rot y", m_RotY);
		serialize.NumberFixed_Unbounded("rot z", m_RotZ);
		serialize.NumberFixed_Unbounded("rot y speed", m_RotYSpeed);
		serialize.NumberFixed_Unbounded("altitude", m_Y);
		serialize.Bool("relative", m_RelativeToGround);
		serialize.Bool("floating", m_Floating);
		serialize.NumberFixed_Unbounded("float depth", m_FloatDepth);
		serialize.NumberFixed_Unbounded("constructionprogress", m_ConstructionProgress);

		if (serialize.IsDebug())
		{
			const char* anchor = "???";
			switch (m_AnchorType)
			{
			case AnchorType::PITCH:
				anchor = "pitch";
				break;

			case AnchorType::PITCH_ROLL:
				anchor = "pitch-roll";
				break;

			case AnchorType::ROLL:
				anchor = "roll";
				break;

			case AnchorType::UPRIGHT: // upright is the default
			default:
				anchor = "upright";
				break;
			}
			serialize.StringASCII("anchor", anchor, 0, 16);
		}
		serialize.NumberU32_Unbounded("turret parent", m_TurretParent);
		if (m_TurretParent != INVALID_ENTITY)
		{
			serialize.NumberFixed_Unbounded("x", m_TurretPosition.X);
			serialize.NumberFixed_Unbounded("y", m_TurretPosition.Y);
			serialize.NumberFixed_Unbounded("z", m_TurretPosition.Z);
		}
		Serializer(serialize, "turrets", m_Turrets);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		MarkRenderDirty();
		Init(paramNode);

		deserialize.Bool("in world", m_InWorld);
		if (m_InWorld)
		{
			deserialize.NumberFixed_Unbounded("x", m_X);
			deserialize.NumberFixed_Unbounded("y", m_Y);
			deserialize.NumberFixed_Unbounded("z", m_Z);
			deserialize.NumberFixed_Unbounded("last x", m_LastX);
			deserialize.NumberFixed_Unbounded("last y diff", m_LastYDifference);
			deserialize.NumberFixed_Unbounded("last z", m_LastZ);
		}

		deserialize.NumberFixed_Unbounded("rot x", m_RotX);
		deserialize.NumberFixed_Unbounded("rot y", m_RotY);
		deserialize.NumberFixed_Unbounded("rot z", m_RotZ);
		deserialize.NumberFixed_Unbounded("rot y speed", m_RotYSpeed);
		deserialize.NumberFixed_Unbounded("altitude", m_Y);
		deserialize.Bool("relative", m_RelativeToGround);
		deserialize.Bool("floating", m_Floating);
		deserialize.NumberFixed_Unbounded("float depth", m_FloatDepth);
		deserialize.NumberFixed_Unbounded("constructionprogress", m_ConstructionProgress);
		// TODO: should there be range checks on all these values?

		m_SnapRotY = true;

		deserialize.NumberU32_Unbounded("turret parent", m_TurretParent);
		if (m_TurretParent != INVALID_ENTITY)
		{
			deserialize.NumberFixed_Unbounded("x", m_TurretPosition.X);
			deserialize.NumberFixed_Unbounded("y", m_TurretPosition.Y);
			deserialize.NumberFixed_Unbounded("z", m_TurretPosition.Z);
		}
		Serializer(deserialize, "turrets", m_Turrets);

		m_SnapRotXZ = true;
	}

	void Deserialized()
	{
		AdvertiseInterpolatedPositionChanges();
	}

	void UpdateTurretPosition() override
	{
		MarkRenderDirty();
		if (m_TurretParent == INVALID_ENTITY)
			return;
		CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
		if (!cmpPosition)
		{
			LOGERROR("Turret with parent without position component");
			return;
		}
		if (!cmpPosition->IsInWorld())
			MoveOutOfWorld();
		else
		{
			CFixedVector2D rotatedPosition = CFixedVector2D(m_TurretPosition.X, m_TurretPosition.Z);
			rotatedPosition = rotatedPosition.Rotate(cmpPosition->GetRotation().Y);
			CFixedVector2D rootPosition = cmpPosition->GetPosition2D();
			entity_pos_t x = rootPosition.X + rotatedPosition.X;
			entity_pos_t z = rootPosition.Y + rotatedPosition.Y;
			if (!m_InWorld || m_X != x || m_Z != z)
				MoveTo(x, z);
			entity_pos_t y = cmpPosition->GetHeightOffset() + m_TurretPosition.Y;
			if (!m_InWorld || GetHeightOffset() != y)
				SetHeightOffset(y);
			m_InWorld = true;
		}
	}

	std::set<entity_id_t>* GetTurrets() override
	{
		return &m_Turrets;
	}

	void SetTurretParent(entity_id_t id, const CFixedVector3D& offset) override
	{
		MarkRenderDirty();
		entity_angle_t angle = GetRotation().Y;
		if (m_TurretParent != INVALID_ENTITY)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
			if (cmpPosition)
				cmpPosition->GetTurrets()->erase(GetEntityId());
		}

		m_TurretParent = id;
		m_TurretPosition = offset;

		if (m_TurretParent != INVALID_ENTITY)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
			if (cmpPosition)
				cmpPosition->GetTurrets()->insert(GetEntityId());
		}
		SetYRotation(angle);
		UpdateTurretPosition();
	}

	entity_id_t GetTurretParent() const override
	{
		return m_TurretParent;
	}

	bool IsInWorld() const override
	{
		return m_InWorld;
	}

	void MoveOutOfWorld() override
	{
		MarkRenderDirty();
		m_InWorld = false;

		AdvertisePositionChanges();
		AdvertiseInterpolatedPositionChanges();
	}

	void MoveTo(entity_pos_t x, entity_pos_t z) override
	{
		MarkRenderDirty();
		m_X = x;
		m_Z = z;

		if (!m_InWorld)
		{
			m_InWorld = true;
			m_LastX = m_PrevX = m_X;
			m_LastZ = m_PrevZ = m_Z;
			m_LastYDifference = entity_pos_t::Zero();
		}

		AdvertisePositionChanges();
		AdvertiseInterpolatedPositionChanges();
	}

	void MoveAndTurnTo(entity_pos_t x, entity_pos_t z, entity_angle_t ry) override
	{
		MarkRenderDirty();
		m_X = x;
		m_Z = z;

		if (!m_InWorld)
		{
			m_InWorld = true;
			m_LastX = m_PrevX = m_X;
			m_LastZ = m_PrevZ = m_Z;
			m_LastYDifference = entity_pos_t::Zero();
		}

		// TurnTo will advertise the position changes
		TurnTo(ry);

		AdvertiseInterpolatedPositionChanges();
	}

	void JumpTo(entity_pos_t x, entity_pos_t z) override
	{
		MarkRenderDirty();
		m_LastX = m_PrevX = m_X = x;
		m_LastZ = m_PrevZ = m_Z = z;
		m_InWorld = true;
		m_SnapRotXZ = true;

		AdvertisePositionChanges();
		AdvertiseInterpolatedPositionChanges();
	}

	void SetHeightOffset(entity_pos_t dy) override
	{
		MarkRenderDirty();
		// subtract the offset and replace with a new offset
		m_LastYDifference = dy - GetHeightOffset();
		m_Y += m_LastYDifference;
		AdvertiseInterpolatedPositionChanges();
	}

	entity_pos_t GetHeightOffset() const override
	{
		if (m_RelativeToGround)
			return m_Y;
		// not relative to the ground, so the height offset is m_Y - ground height
		// except when floating, when the height offset is m_Y - water level + float depth
		entity_pos_t baseY;
		CmpPtr<ICmpTerrain> cmpTerrain(GetSystemEntity());
		if (cmpTerrain)
			baseY = cmpTerrain->GetGroundLevel(m_X, m_Z);

		if (m_Floating)
		{
			CmpPtr<ICmpWaterManager> cmpWaterManager(GetSystemEntity());
			if (cmpWaterManager)
				baseY = std::max(baseY, cmpWaterManager->GetWaterLevel(m_X, m_Z) - m_FloatDepth);
		}
		return m_Y - baseY;
	}

	void SetHeightFixed(entity_pos_t y) override
	{
		MarkRenderDirty();
		// subtract the absolute height and replace it with a new absolute height
		m_LastYDifference = y - GetHeightFixed();
		m_Y += m_LastYDifference;
		AdvertiseInterpolatedPositionChanges();
	}

	entity_pos_t GetHeightFixed() const override
	{
		return GetHeightAtFixed(m_X, m_Z);
	}

	entity_pos_t GetHeightAtFixed(entity_pos_t x, entity_pos_t z) const override
	{
		if (!m_RelativeToGround)
			return m_Y;
		// relative to the ground, so the fixed height = ground height + m_Y
		// except when floating, when the fixed height = water level - float depth + m_Y
		entity_pos_t baseY;
		CmpPtr<ICmpTerrain> cmpTerrain(GetSystemEntity());
		if (cmpTerrain)
			baseY = cmpTerrain->GetGroundLevel(x, z);

		if (m_Floating)
		{
			CmpPtr<ICmpWaterManager> cmpWaterManager(GetSystemEntity());
			if (cmpWaterManager)
				baseY = std::max(baseY, cmpWaterManager->GetWaterLevel(x, z) - m_FloatDepth);
		}
		return m_Y + baseY;
	}

	bool IsHeightRelative() const override
	{
		return m_RelativeToGround;
	}

	void SetHeightRelative(bool relative) override
	{
		MarkRenderDirty();
		// move y to use the right offset (from terrain or from map origin)
		m_Y = relative ? GetHeightOffset() : GetHeightFixed();
		m_RelativeToGround = relative;
		m_LastYDifference = entity_pos_t::Zero();
		AdvertiseInterpolatedPositionChanges();
	}

	bool CanFloat() const override
	{
		return m_Floating;
	}

	void SetFloating(bool flag) override
	{
		MarkRenderDirty();
		m_Floating = flag;
		AdvertiseInterpolatedPositionChanges();
	}

	void SetConstructionProgress(fixed progress) override
	{
		MarkRenderDirty();
		m_ConstructionProgress = progress;
		AdvertiseInterpolatedPositionChanges();
	}

	CFixedVector3D GetPosition() const override
	{
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetPosition called on entity when IsInWorld is false");
			return CFixedVector3D();
		}

		return CFixedVector3D(m_X, GetHeightFixed(), m_Z);
	}

	CFixedVector2D GetPosition2D() const override
	{
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetPosition2D called on entity when IsInWorld is false");
			return CFixedVector2D();
		}

		return CFixedVector2D(m_X, m_Z);
	}

	CFixedVector3D GetPreviousPosition() const override
	{
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetPreviousPosition called on entity when IsInWorld is false");
			return CFixedVector3D();
		}

		return CFixedVector3D(m_PrevX, GetHeightAtFixed(m_PrevX, m_PrevZ), m_PrevZ);
	}

	CFixedVector2D GetPreviousPosition2D() const override
	{
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetPreviousPosition2D called on entity when IsInWorld is false");
			return CFixedVector2D();
		}

		return CFixedVector2D(m_PrevX, m_PrevZ);
	}

	fixed GetTurnRate() const override
	{
		return m_RotYSpeed;
	}

	void TurnTo(entity_angle_t y) override
	{
		MarkRenderDirty();
		if (m_TurretParent != INVALID_ENTITY)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
			if (cmpPosition)
				y -= cmpPosition->GetRotation().Y;
		}

		m_RotY = y;

		AdvertisePositionChanges();
	}

	void SetYRotation(entity_angle_t y) override
	{
		MarkRenderDirty();
		if (m_TurretParent != INVALID_ENTITY)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
			if (cmpPosition)
				y -= cmpPosition->GetRotation().Y;
		}
		m_RotY = y;
		m_SnapRotY = true;
		m_SnapRotXZ = true;

		AdvertisePositionChanges();
	}

	void SetXZRotation(entity_angle_t x, entity_angle_t z) override
	{
		MarkRenderDirty();
		m_RotX = x;
		m_RotZ = z;
		m_SnapRotXZ = true;
	}

	CFixedVector3D GetRotation() const override
	{
		entity_angle_t y = m_RotY;
		if (m_TurretParent != INVALID_ENTITY)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
			if (cmpPosition)
				y += cmpPosition->GetRotation().Y;
		}
		return CFixedVector3D(m_RotX, y, m_RotZ);
	}

	fixed GetDistanceTravelled() const override
	{
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetDistanceTravelled called on entity when IsInWorld is false");
			return fixed::Zero();
		}

		return CFixedVector2D(m_X - m_LastX, m_Z - m_LastZ).Length();
	}

	void GetInterpolatedPosition2D(float frameOffset, float& x, float& z, float& rotY) const override
	{
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetInterpolatedPosition2D called on entity when IsInWorld is false");
			return;
		}

		x = Interpolate(m_LastX.ToFloat(), m_X.ToFloat(), frameOffset);
		z = Interpolate(m_LastZ.ToFloat(), m_Z.ToFloat(), frameOffset);

		rotY = m_RotY.ToFloat();
	}

	CMatrix3D GetInterpolatedTransform(float frameOffset) const override
	{
		CMatrix3D m;
		m.SetIdentity();
		if (m_TurretParent != INVALID_ENTITY)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), m_TurretParent);
			if (!cmpPosition)
			{
				LOGERROR("Turret with parent without position component");
				return m;
			}
			if (!cmpPosition->IsInWorld())
			{
				LOGERROR("CCmpPosition::GetInterpolatedTransform called on turret entity when IsInWorld is false");
				return m;
			}
			CMatrix3D ownTransformation;
			ownTransformation.SetYRotation(m_RotY.ToFloat());
			ownTransformation.Translate(-m_TurretPosition.X.ToFloat(), m_TurretPosition.Y.ToFloat(), -m_TurretPosition.Z.ToFloat());
			return cmpPosition->GetInterpolatedTransform(frameOffset) * ownTransformation;
		}
		if (!m_InWorld)
		{
			LOGERROR("CCmpPosition::GetInterpolatedTransform called on entity when IsInWorld is false");
			return m;
		}

		float x{0.0f}, z{0.0f}, rotY{0.0f};
		GetInterpolatedPosition2D(frameOffset, x, z, rotY);

		float baseY = 0;
		if (m_RelativeToGround)
		{
			CmpPtr<ICmpTerrain> cmpTerrain(GetSystemEntity());
			if (cmpTerrain)
				baseY = cmpTerrain->GetExactGroundLevel(x, z);

			if (m_Floating)
			{
				CmpPtr<ICmpWaterManager> cmpWaterManager(GetSystemEntity());
				if (cmpWaterManager)
					baseY = std::max(baseY, cmpWaterManager->GetExactWaterLevel(x, z) - m_FloatDepth.ToFloat());
			}
		}

		const float y = baseY + m_Y.ToFloat() + Interpolate(-1 * m_LastYDifference.ToFloat(), 0.f, frameOffset);

		m.SetXRotation(m_RotX.ToFloat());
		m.RotateZ(m_RotZ.ToFloat());
		m.RotateY(rotY + std::numbers::pi_v<float>);
		m.Translate(CVector3D(x, y, z));
		return m;
	}

	void GetInterpolatedPositions(CVector3D& pos0, CVector3D& pos1) const
	{
		float baseY0 = 0;
		float baseY1 = 0;
		float x0 = m_LastX.ToFloat();
		float z0 = m_LastZ.ToFloat();
		float x1 = m_X.ToFloat();
		float z1 = m_Z.ToFloat();
		if (m_RelativeToGround)
		{
			CmpPtr<ICmpTerrain> cmpTerrain(GetSimContext(), SYSTEM_ENTITY);
			if (cmpTerrain)
			{
				baseY0 = cmpTerrain->GetExactGroundLevel(x0, z0);
				baseY1 = cmpTerrain->GetExactGroundLevel(x1, z1);
			}

			if (m_Floating)
			{
				CmpPtr<ICmpWaterManager> cmpWaterManager(GetSimContext(), SYSTEM_ENTITY);
				if (cmpWaterManager)
				{
					baseY0 = std::max(baseY0, cmpWaterManager->GetExactWaterLevel(x0, z0) - m_FloatDepth.ToFloat());
					baseY1 = std::max(baseY1, cmpWaterManager->GetExactWaterLevel(x1, z1) - m_FloatDepth.ToFloat());
				}
			}
		}

		float y0 = baseY0 + m_Y.ToFloat() + m_LastYDifference.ToFloat();
		float y1 = baseY1 + m_Y.ToFloat();

		pos0 = CVector3D(x0, y0, z0);
		pos1 = CVector3D(x1, y1, z1);
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_FrameStep:
		{
			PublishRenderState();
			break;
		}
		case MT_TurnStart:
		{
			m_TurnStarted = true;
			if (m_InWorld && (m_LastX != m_X || m_LastZ != m_Z))
				m_TurnMoved = true;
			if (m_TurnMoved || !m_LastYDifference.IsZero())
				MarkRenderDirty();

			// Store the positions from the turn before
			m_PrevX = m_LastX;
			m_PrevZ = m_LastZ;

			m_LastX = m_X;
			m_LastZ = m_Z;
			m_LastYDifference = entity_pos_t::Zero();

			break;
		}
		case MT_TerrainChanged:
		case MT_WaterChanged:
		{
			AdvertiseInterpolatedPositionChanges();
			break;
		}
		case MT_Deserialized:
		{
			Deserialized();
			break;
		}
		}
	}

private:

	void MarkRenderDirty()
	{
		if (m_RenderDirty)
			return;
		m_RenderDirty = true;
		GetSimContext().GetComponentManager().RequestFrameStep(this);
	}

	/**
	 * Record the simulation state and the rotation events since the last
	 * record for the render world.
	 */
	void PublishRenderState()
	{
		m_RenderDirty = false;
		RenderWorld::CFramePacket* packet = GetSimContext().GetFramePacket();
		if (!packet)
			return;
		RenderWorld::SPosition& r = packet->positions.emplace_back();
		r.entity = GetEntityId();
		r.inWorld = m_InWorld;
		r.x = m_X.ToFloat();
		r.z = m_Z.ToFloat();
		r.lastX = m_LastX.ToFloat();
		r.lastZ = m_LastZ.ToFloat();
		r.y = m_Y.ToFloat();
		r.lastYDifference = m_LastYDifference.ToFloat();
		r.relativeToGround = m_RelativeToGround;
		r.floating = m_Floating;
		r.floatDepth = m_FloatDepth.ToFloat();
		r.constructionProgress = m_ConstructionProgress.ToFloat();
		r.rotX = m_RotX.ToFloat();
		r.rotY = m_RotY.ToFloat();
		r.rotZ = m_RotZ.ToFloat();
		r.rotYSpeed = m_RotYSpeed.ToFloat();
		r.anchorType = m_AnchorType;
		r.turretParent = m_TurretParent;
		if (m_TurretParent != INVALID_ENTITY)
			r.turretPosition = CVector3D(m_TurretPosition);
		r.turnStarted = m_TurnStarted;
		r.turnMoved = m_TurnMoved;
		r.snapRotY = m_SnapRotY;
		r.snapRotXZ = m_SnapRotXZ;
		m_TurnStarted = m_TurnMoved = m_SnapRotY = m_SnapRotXZ = false;
	}

	/**
	 * This must be called after changing anything that will affect the
	 * return value of GetPosition2D() or GetRotation().Y:
	 *  - m_InWorld
	 *  - m_X, m_Z
	 *  - m_RotY
	 */
	void AdvertisePositionChanges() const
	{
		for (std::set<entity_id_t>::const_iterator it = m_Turrets.begin(); it != m_Turrets.end(); ++it)
		{
			CmpPtr<ICmpPosition> cmpPosition(GetSimContext(), *it);
			if (cmpPosition)
				cmpPosition->UpdateTurretPosition();
		}
		if (m_InWorld)
		{
			CMessagePositionChanged msg(GetEntityId(), true, m_X, m_Z, m_RotY);
			GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
		}
		else
		{
			CMessagePositionChanged msg(GetEntityId(), false, entity_pos_t::Zero(), entity_pos_t::Zero(), entity_angle_t::Zero());
			GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
		}
	}

	/**
	 * This must be called after changing anything that will affect the
	 * return value of GetInterpolatedPositions():
	 *  - m_InWorld
	 *  - m_X, m_Z
	 *  - m_LastX, m_LastZ
	 *  - m_Y, m_LastYDifference, m_RelativeToGround
	 *  - If m_RelativeToGround, then the ground under this unit
	 *  - If m_RelativeToGround && m_Float, then the water level
	 */
	void AdvertiseInterpolatedPositionChanges() const
	{
		if (m_InWorld)
		{
			CVector3D pos0, pos1;
			GetInterpolatedPositions(pos0, pos1);

			CMessageInterpolatedPositionChanged msg(GetEntityId(), true, pos0, pos1);
			GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
		}
		else
		{
			CMessageInterpolatedPositionChanged msg(GetEntityId(), false, CVector3D(), CVector3D());
			GetSimContext().GetComponentManager().PostMessage(GetEntityId(), msg);
		}
	}
};

REGISTER_COMPONENT_TYPE(Position)
