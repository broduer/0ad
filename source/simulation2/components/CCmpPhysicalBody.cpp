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

#include "ICmpPhysicalBody.h"

#include "simulation2/helpers/Position.h"
#include "simulation2/system/Component.h"

#include <cstdint>
#include <string>

class CCmpPhysicalBody final : public ICmpPhysicalBody
{
public:
	static void ClassInit(CComponentManager&)
	{
	}

	DEFAULT_COMPONENT_ALLOCATOR(PhysicalBody)

	entity_pos_t m_Weight;
	bool m_PushingEnabled;
	uint8_t m_Pressure;

	static std::string GetSchema()
	{
		return
			"<a:help>The physical properties of a unit, which decide how it pushes other units and is pushed by them.</a:help>"
			"<element name='Weight' a:help='Makes this unit both push harder and harder to push. 10 is considered the base value.'>"
				"<ref name='positiveDecimal'/>"
			"</element>";
	}

	void Init(const CParamNode& paramNode) override
	{
		m_Weight = paramNode.GetChild("Weight").ToFixed();
		// UnitMotion turns this on for the units that take part (see SetEnabled).
		m_PushingEnabled = false;
		m_Pressure = 0;
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serialize) override
	{
		serialize.Bool("pushingEnabled", m_PushingEnabled);
		serialize.NumberU8_Unbounded("pressure", m_Pressure);
	}

	void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override
	{
		Init(paramNode);

		deserialize.Bool("pushingEnabled", m_PushingEnabled);
		deserialize.NumberU8_Unbounded("pressure", m_Pressure);
	}

	void SetEnabled(bool enabled) override
	{
		m_PushingEnabled = enabled;
	}

	bool IsPushingEnabled() const override
	{
		return m_PushingEnabled;
	}

	entity_pos_t GetWeight() const override
	{
		return m_Weight;
	}

	uint8_t GetPressure() const override
	{
		return m_Pressure;
	}

	void SetPressure(uint8_t pressure) override
	{
		m_Pressure = pressure;
	}
};

REGISTER_COMPONENT_TYPE(PhysicalBody)
