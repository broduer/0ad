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

#ifndef INCLUDED_PUSHCONFIG
#define INCLUDED_PUSHCONFIG

#include "maths/MathUtil.h"
#include "ps/CLogger.h"
#include "simulation2/system/ParamNode.h"
#include "simulation2/helpers/Position.h"

class PushConfig
{
public:
	/**
	 * Maximum value for pushing pressure.
	 */
	static constexpr int MAX_PRESSURE = 255;

	/**
	 * For pushing, treat the clearances as a circle - they're defined as squares,
	 * so we'll take the circumscribing square (approximately).
	 * Clerances are also full-width instead of half, so we want to divide by two. sqrt(2)/2 is about 0.71 < 5/7.
	 */
	static constexpr entity_pos_t PUSHING_CORRECTION = entity_pos_t::FromFraction(5, 7);

	/**
	 * Arbitrary constant used to reduce pushing to levels that won't break physics for our turn length.
	 */
	static constexpr int PUSHING_REDUCTION_FACTOR = 2;

	/**
	 * Maximum distance-related multiplier.
	 * NB: this value interacts with the "minimal pushing" force,
	 * as two perfectly overlapping units exert MAX_DISTANCE_FACTOR * Turn length in ms / REDUCTION_FACTOR
	 * of force on each other each turn. If this is below the minimal pushing force, any 2 units can entirely overlap.
	 */
	static constexpr entity_pos_t MAX_DISTANCE_FACTOR = entity_pos_t::FromFraction(5, 2);

	/**
	 * Maximum pushing multiplier for a single push calculation.
	 * This exists for numerical stability of the system between a lightweight and a heavy unit.
	 */
	static constexpr int MAX_PUSHING_MULTIPLIER = 4;

	/**
	 * When two units are about to cross paths, if the dot product of their offsets before and after the move
	 * is below this value, give them a perpendicular nudge instead of trying to push in the regular way.
	 */
	static constexpr entity_pos_t PERPENDICULAR_NUDGE_THRESHOLD = entity_pos_t::FromFraction(-1, 10);

	/**
	 * Pushing is dampened by pushing pressure, but this is capped so that units still get pushed.
	 */
	static constexpr int MAX_PUSH_DAMPING_PRESSURE = 160;
	static_assert(MAX_PUSH_DAMPING_PRESSURE < MAX_PRESSURE);

	/**
	 * When units are obstructed because they're being pushed away from where they want to go,
	 * raise the pushing pressure to at least this value.
	 */
	static constexpr int MIN_PRESSURE_IF_OBSTRUCTED = 80;

	/**
	 * These two numbers are used to calculate pushing pressure between two units.
	 */
	static constexpr entity_pos_t PRESSURE_STATIC_FACTOR = entity_pos_t::FromInt(2);
	static constexpr int PRESSURE_DISTANCE_FACTOR = 5;

	struct RuntimeConfig
	{
		entity_pos_t radiusMultiplier;
		entity_pos_t movingPushExtension;
		entity_pos_t staticPushExtension;
		entity_pos_t movingPushingSpread;
		entity_pos_t staticPushingSpread;
		entity_pos_t minimalPushing;
		entity_pos_t pressureStrength;
		entity_pos_t pressureDecay;

		bool IsEnabled() const { return radiusMultiplier != entity_pos_t::Zero(); }

		RuntimeConfig()
		{
			radiusMultiplier = entity_pos_t::FromInt(8) / 5;  // 1.6
			movingPushExtension = entity_pos_t::FromInt(5) / 2;  // 2.5 meters
			staticPushExtension = entity_pos_t::FromInt(2);  // 2 meters
			movingPushingSpread = entity_pos_t::FromFraction(5, 8);  // 0.625
			staticPushingSpread = entity_pos_t::FromFraction(5, 8);  // 0.625
			minimalPushing = entity_pos_t::FromFraction(2, 10);  // 0.2
			pressureStrength = entity_pos_t::FromInt(1);
			pressureDecay = entity_pos_t::FromInt(6) / 10;  // 0.6
		}

		// Validation helper
		void Validate()
		{
			if (radiusMultiplier < entity_pos_t::Zero())
				radiusMultiplier = entity_pos_t::Zero();

			if (movingPushExtension < entity_pos_t::Zero())
				movingPushExtension = entity_pos_t::Zero();
			if (staticPushExtension < entity_pos_t::Zero())
				staticPushExtension = entity_pos_t::Zero();

			if (movingPushingSpread < entity_pos_t::Zero())
				movingPushingSpread = entity_pos_t::Zero();
			if (movingPushingSpread > entity_pos_t::FromInt(1))
				movingPushingSpread = entity_pos_t::FromInt(1);

			if (staticPushingSpread < entity_pos_t::Zero())
				staticPushingSpread = entity_pos_t::Zero();
			if (staticPushingSpread > entity_pos_t::FromInt(1))
				staticPushingSpread = entity_pos_t::FromInt(1);

			if (minimalPushing < entity_pos_t::Zero())
				minimalPushing = entity_pos_t::Zero();

			if (pressureStrength < entity_pos_t::Zero())
				pressureStrength = entity_pos_t::Zero();

			if (pressureDecay < entity_pos_t::Zero())
				pressureDecay = entity_pos_t::Zero();
			if (pressureDecay > entity_pos_t::FromInt(1))
				pressureDecay = entity_pos_t::FromInt(1);
		}
	};

	static PushConfig& GetInstance()
	{
		static PushConfig instance;
		return instance;
	}

	void EnsureLoaded()
	{
		if (!m_Loaded)
			LoadFromXML();
	}

	/**
	 * Load the settings from pathfinder.xml, at the start of each game.
	 */
	void Reload()
	{
		m_Loaded = false;
		m_RuntimeConfig = RuntimeConfig();
		LoadFromXML();
	}

	void LoadFromXML()
	{
		if (m_Loaded)
			return;

		CParamNode externalParamNode;
		CParamNode::LoadXML(externalParamNode, L"simulation/data/pathfinder.xml", "pathfinder");

		CParamNode pushingNode = externalParamNode.GetChild("Pathfinder").GetChild("Pushing");
		// Without a Pushing section (e.g. in test data), keep the defaults.
		if (!pushingNode.IsOk())
		{
			m_Loaded = true;
			return;
		}

		const CParamNode radius = pushingNode.GetChild("Radius");
		if (radius.IsOk())
		{
			m_RuntimeConfig.radiusMultiplier = radius.ToFixed();
			if (m_RuntimeConfig.radiusMultiplier < entity_pos_t::Zero())
			{
				LOGWARNING("PushConfig: Pushing radius multiplier cannot be below 0, using default");
				m_RuntimeConfig.radiusMultiplier = RuntimeConfig().radiusMultiplier;
			}
		}

		const CParamNode movingExt = pushingNode.GetChild("MovingExtension");
		const CParamNode staticExt = pushingNode.GetChild("StaticExtension");
		if (movingExt.IsOk() && staticExt.IsOk())
		{
			m_RuntimeConfig.movingPushExtension = movingExt.ToFixed();
			m_RuntimeConfig.staticPushExtension = staticExt.ToFixed();
		}

		const CParamNode movingSpread = pushingNode.GetChild("MovingSpread");
		const CParamNode staticSpread = pushingNode.GetChild("StaticSpread");
		if (movingSpread.IsOk() && staticSpread.IsOk())
		{
			m_RuntimeConfig.movingPushingSpread = movingSpread.ToFixed();
			m_RuntimeConfig.staticPushingSpread = staticSpread.ToFixed();
		}

		const CParamNode minForce = pushingNode.GetChild("MinimalForce");
		if (minForce.IsOk())
		{
			m_RuntimeConfig.minimalPushing = minForce.ToFixed();
			if (m_RuntimeConfig.minimalPushing < entity_pos_t::Zero())
			{
				LOGWARNING("PushConfig: MinimalForce cannot be negative, using default");
				m_RuntimeConfig.minimalPushing = RuntimeConfig().minimalPushing;
			}
		}

		const CParamNode pressureStrengthNode = pushingNode.GetChild("PressureStrength");
		const CParamNode pressureDecayNode = pushingNode.GetChild("PressureDecay");
		if (pressureStrengthNode.IsOk() && pressureDecayNode.IsOk())
		{
			m_RuntimeConfig.pressureStrength = pressureStrengthNode.ToFixed();
			if (m_RuntimeConfig.pressureStrength < entity_pos_t::Zero())
			{
				LOGWARNING("PushConfig: Pushing pressure strength cannot be below 0, using default");
				m_RuntimeConfig.pressureStrength = RuntimeConfig().pressureStrength;
			}

			m_RuntimeConfig.pressureDecay = pressureDecayNode.ToFixed();
		}

		// Apply validation to all values
		m_RuntimeConfig.Validate();
		m_Loaded = true;
	}

	const RuntimeConfig& GetRuntimeConfig() const
	{
		return m_RuntimeConfig;
	}

	bool IsEnabled() const
	{
		return m_Loaded && m_RuntimeConfig.IsEnabled();
	}

private:
	PushConfig() : m_Loaded(false) {}
	PushConfig(const PushConfig&) = delete;
	PushConfig& operator=(const PushConfig&) = delete;

	RuntimeConfig m_RuntimeConfig;
	bool m_Loaded;
};

#endif // INCLUDED_PUSHCONFIG