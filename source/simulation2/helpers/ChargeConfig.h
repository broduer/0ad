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

#ifndef INCLUDED_CHARGECONFIG
#define INCLUDED_CHARGECONFIG

#include "maths/Fixed.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Filesystem.h"
#include "simulation2/system/ParamNode.h"

#include <limits>

/**
 * How a charge hit shapes its force, shared by all units, from simulation/data/charge.xml.
 * Settings missing from the file, or the whole file, keep the defaults below.
 */
class ChargeConfig
{
public:
	struct Values
	{
		// Spin is the angular half of the hit's own impulse, so it needs no strength of its own; this only
		// scales the physical result up or down for taste.
		fixed spinScale{fixed::FromInt(1)};
		fixed rollStrength{fixed::FromFraction(1, 10)};      // Roll impulse per unit of sideways velocity change
		fixed rollImpulseMin{fixed::FromFraction(1, 1000)};  // Smaller roll impulses are dropped

		// Spin and roll after the hit, integrated by ImpactEffects
		fixed spinMaxVelocity{fixed::FromInt(6)};                 // Fastest spin, in radians/second
		fixed spinDecay{fixed::FromFraction(15, 100)};            // Share of the spin lost every IMPACT_REFERENCE_TIME
		fixed spinRestVelocity{fixed::FromFraction(5, 100)};      // Slower spins (radians/second) stop
		fixed rollMaxVelocityScale{fixed::FromInt(2)};            // Fastest roll, in max roll angles per second
		fixed rollSpring{fixed::FromFraction(18, 10)};            // Stiffness of the spring back to level
		fixed rollDamping{fixed::FromFraction(4, 10)};            // Damping of the spring back to level
		fixed rollOvershootScale{fixed::FromFraction(150, 100)};  // How far past the max roll angle the spring may swing
		fixed rollRestVelocity{fixed::FromFraction(1, 1000)};     // Slower rolls stop
		fixed rollSettledAngle{fixed::FromFraction(1, 100)};      // A roll at rest below this (radians) is levelled

		// Damage
		bool fullBlowAtRunSpeed{false};  // A full blow is a hit at the charger's run speed rather than its walk speed
	};

	static const Values& Get()
	{
		return Mutable();
	}

	/**
	 * Load charge.xml, at the start of each game.
	 */
	static void Reload()
	{
		Values& values{Mutable()};
		values = Values();

		const VfsPath path{L"simulation/data/charge.xml"};
		if (!VfsFileExists(path))
			return;

		CParamNode externalParamNode;
		CParamNode::LoadXML(externalParamNode, path, "");
		const CParamNode& node{externalParamNode.GetChild("Charge")};
		if (!node.IsOk())
			return;

		const fixed zero{fixed::Zero()};
		const fixed one{fixed::FromInt(1)};
		const fixed unbounded{std::numeric_limits<fixed>::max()};

		Read(node, "SpinScale", values.spinScale, zero, unbounded);
		Read(node, "RollStrength", values.rollStrength, zero, unbounded);
		Read(node, "RollImpulseMin", values.rollImpulseMin, zero, unbounded);
		Read(node, "SpinMaxVelocity", values.spinMaxVelocity, zero, unbounded);
		Read(node, "SpinDecay", values.spinDecay, zero, one);
		Read(node, "SpinRestVelocity", values.spinRestVelocity, zero, unbounded);
		Read(node, "RollMaxVelocity", values.rollMaxVelocityScale, zero, unbounded);
		Read(node, "RollSpring", values.rollSpring, zero, unbounded);
		Read(node, "RollDamping", values.rollDamping, zero, unbounded);
		Read(node, "RollOvershoot", values.rollOvershootScale, zero, unbounded);
		Read(node, "RollRestVelocity", values.rollRestVelocity, zero, unbounded);
		Read(node, "RollSettledAngle", values.rollSettledAngle, zero, unbounded);

		const CParamNode& fullBlowSpeed{node.GetChild("FullBlowSpeed")};
		if (fullBlowSpeed.IsOk())
		{
			const std::string& speed{fullBlowSpeed.ToString()};
			if (speed == "walk" || speed == "run")
				values.fullBlowAtRunSpeed = speed == "run";
			else
				LOGWARNING("ChargeConfig: FullBlowSpeed must be walk or run, using the default");
		}

	}

private:
	static Values& Mutable()
	{
		static Values values;
		return values;
	}

	/**
	 * Set value from the child name of node, if there is one within [min, max].
	 */
	static void Read(const CParamNode& node, const char* name, fixed& value, const fixed min, const fixed max)
	{
		const CParamNode& child{node.GetChild(name)};
		if (!child.IsOk())
			return;

		const fixed loaded{child.ToFixed()};
		if (loaded < min || loaded > max)
		{
			LOGWARNING("ChargeConfig: %s must be between %s and %s, using the default %s",
				name, min.ToString(), max.ToString(), value.ToString());
			return;
		}
		value = loaded;
	}
};

#endif // INCLUDED_CHARGECONFIG
