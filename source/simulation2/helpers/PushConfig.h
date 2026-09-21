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

#include <algorithm>

class PushConfig
{
public:
	// These are truly constant (derived from physics/math, not gameplay tuning)
	static constexpr int MAX_PRESSURE = 255;
	static constexpr entity_pos_t PUSHING_CORRECTION = entity_pos_t::FromFraction(5, 7);  // ~0.714 (sqrt(2)/2)
	static constexpr int PUSHING_REDUCTION_FACTOR = 2;
	static constexpr entity_pos_t MAX_DISTANCE_FACTOR = entity_pos_t::FromFraction(5, 2);  // 2.5
	static constexpr int MAX_PUSHING_MULTIPLIER = 4;
	static constexpr entity_pos_t PRESSURE_STATIC_FACTOR = entity_pos_t::FromInt(2);
	static constexpr int PRESSURE_DISTANCE_FACTOR = 5;
	static constexpr int PUSHING_GRID_SIZE = 20;  // meters per grid cell
	// The body radius (the 'default' passability class clearance) the per-turn push amounts above were tuned
	// at. A full push moves a unit about a quarter of a metre a turn whatever its size: a third of a
	// footman's radius, a twenty-fourth of a warship's beam. Bodies that collide as their footprint (ships,
	// the largest elephants) scale their pushes up by the square root of their radius over this one
	// (see UnitCollision::ComputePairPush for why not the ratio itself).
	static constexpr entity_pos_t REFERENCE_RADIUS = entity_pos_t::FromFraction(4, 5);
	// Seconds that decay factors (e.g. Drifting) are defined over, so the effects last as long at any turn length.
	// 0.2 s is the single player turn length the effects were tuned at.
	static constexpr fixed IMPACT_REFERENCE_TIME = fixed::FromFraction(1, 5);

	/**
	 * The fraction of a decaying quantity left after dt seconds, when it keeps keepPerReference of itself every
	 * IMPACT_REFERENCE_TIME. Lets per-turn style factors (Drifting, stagger recovery) take the same time at any turn length.
	 * Fixed point has no deterministic pow: whole reference steps are applied exactly and the remainder linearly,
	 * which is exact at 200 ms turns and within about 0.1% of the exponential at 500 ms.
	 */
	static fixed DecayFactor(const fixed keepPerReference, const fixed dt)
	{
		if (dt <= fixed::Zero())
			return fixed::FromInt(1);

		const fixed keep{std::max(keepPerReference, fixed::Zero())};
		fixed factor{fixed::FromInt(1)};
		fixed remaining{dt};
		while (remaining >= IMPACT_REFERENCE_TIME && factor > fixed::Zero())
		{
			factor = factor.Multiply(keep);
			remaining -= IMPACT_REFERENCE_TIME;
		}
		const fixed partialKeep{std::max(fixed::FromInt(1) - (fixed::FromInt(1) - keep).Multiply(remaining / IMPACT_REFERENCE_TIME), fixed::Zero())};
		return factor.Multiply(partialKeep);
	}

	// These could be runtime-configurable, but are currently hardcoded
	static constexpr int MAX_PUSH_DAMPING_PRESSURE = 160;  // Cap for pressure-based damping

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
		// The extensions between two solid units, in place of the ones above, in metres. A body pushed from
		// further away is turned around by the soft push before it touches, so fewer of its contacts are
		// rigid ones. Solid against non-solid keeps the plain extensions.
		entity_pos_t solidMovingPushExtension;
		entity_pos_t solidStaticPushExtension;
		// How much harder two solid units push each other than their size alone gives (see
		// UnitCollision::ComputePairPush). Solid against non-solid keeps the plain push.
		entity_pos_t solidPushScale;
		// The fastest approach between two touching solid bodies that is still a brush, left to the soft
		// push rather than stopped by a contact impulse (see UnitCollision::ComputePairPush), in m/s.
		entity_pos_t solidBrushSpeed;
		// The share of the physical spin a soft push gives a solid unit when it lands off its centre. The soft
		// push eases an overlap apart every turn, and its full spin twists a unit on an order against its own
		// steering turn after turn.
		entity_pos_t solidPushSpin;

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
			solidMovingPushExtension = entity_pos_t::FromInt(15) / 2;  // 7.5 meters
			solidStaticPushExtension = entity_pos_t::FromInt(6);  // 6 meters
			solidPushScale = entity_pos_t::FromInt(1);
			solidBrushSpeed = entity_pos_t::FromInt(3);  // m/s
			solidPushSpin = entity_pos_t::FromFraction(1, 4);
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

			if (solidMovingPushExtension < entity_pos_t::Zero())
				solidMovingPushExtension = entity_pos_t::Zero();
			if (solidStaticPushExtension < entity_pos_t::Zero())
				solidStaticPushExtension = entity_pos_t::Zero();
			if (solidPushScale < entity_pos_t::Zero())
				solidPushScale = entity_pos_t::Zero();
			if (solidBrushSpeed < entity_pos_t::Zero())
				solidBrushSpeed = entity_pos_t::Zero();
			if (solidPushSpin < entity_pos_t::Zero())
				solidPushSpin = entity_pos_t::Zero();
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

		const CParamNode solidMovingExtensionNode = pushingNode.GetChild("SolidMovingExtension");
		if (solidMovingExtensionNode.IsOk())
			m_RuntimeConfig.solidMovingPushExtension = solidMovingExtensionNode.ToFixed();

		const CParamNode solidStaticExtensionNode = pushingNode.GetChild("SolidStaticExtension");
		if (solidStaticExtensionNode.IsOk())
			m_RuntimeConfig.solidStaticPushExtension = solidStaticExtensionNode.ToFixed();

		const CParamNode solidPushScaleNode = pushingNode.GetChild("SolidPushScale");
		if (solidPushScaleNode.IsOk())
			m_RuntimeConfig.solidPushScale = solidPushScaleNode.ToFixed();

		const CParamNode solidBrushSpeedNode = pushingNode.GetChild("SolidBrushSpeed");
		if (solidBrushSpeedNode.IsOk())
			m_RuntimeConfig.solidBrushSpeed = solidBrushSpeedNode.ToFixed();

		const CParamNode solidPushSpinNode = pushingNode.GetChild("SolidPushSpin");
		if (solidPushSpinNode.IsOk())
			m_RuntimeConfig.solidPushSpin = solidPushSpinNode.ToFixed();

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

	// Accessors for hardcoded constants (for consistency)

private:
	PushConfig() : m_Loaded(false) {}
	PushConfig(const PushConfig&) = delete;
	PushConfig& operator=(const PushConfig&) = delete;

	RuntimeConfig m_RuntimeConfig;
	bool m_Loaded;
};

#endif // INCLUDED_PUSHCONFIG