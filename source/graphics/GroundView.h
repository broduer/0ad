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

#ifndef INCLUDED_GROUNDVIEW
#define INCLUDED_GROUNDVIEW

class CTerrain;

/**
 * The ground and water as seen by the thread that owns the models, which query it instead of
 * the simulation.
 */
class IGroundView
{
public:
	virtual float GetExactGroundLevel(float x, float z) const = 0;
	virtual float GetWaterLevel() const = 0;
	virtual CTerrain* GetTerrain() const = 0;

protected:
	~IGroundView() = default;
};

#endif // INCLUDED_GROUNDVIEW
