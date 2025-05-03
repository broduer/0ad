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

#include "Sqrt.h"

namespace {
// Based on http://freaknet.org/martin/tape/gos/misc/personal/msc/sqrt/sqrt.c
// A little slow, so simply used to precompute the tables below.
constexpr u32 isqrt64_slow(u64 n)
{
	u64 op = n;
	u64 res = 0;
	u64 one = static_cast<u64>(1) << 62; // highest power of four <= than the argument

	while (one > op)
		one >>= 2;

	while (one != 0)
	{
		if (op >= res + one)
		{
			op -= (res + one);
			res += (one << 1);
		}
		res >>= 1;
		one >>= 2;
	}
	return static_cast<u32>(res);
}

constexpr size_t TABLE_SIZE = 65536;
std::array<u32, TABLE_SIZE> init_sqrt_tables()
{
	std::array<u32, TABLE_SIZE> values;
	// Shift them down 1, as we'd never use the 0th entry otherwise.
	for (size_t i = 0; i < TABLE_SIZE; ++i)
		values[i] = isqrt64_slow((i + 1) << 48);

	return values;
}

// This would be constexpr but it currently takes too many steps.
const auto sqrt_lut = init_sqrt_tables();
} // namespace

// Compute a fast approximation of the square root of a 64-bit integer.
// Uses a lookup table and interpolation for the initial estimate,
// then refines it with two Newton iterations.
// This is not exact; in testing it stays within about 1 of floor(sqrt(n)).
u32 isqrt64(u64 n)
{
	// Extract 16-bit chunks
	u8 shift = 0;
	u16 high = n >> 48;
	u16 mid = n >> 32;

	if (high > 0)
	{
		// Nothing to do
	}
	else if (mid > 0)
	{
		shift = 8;
		high = mid;
		mid = n >> 16;
	}
	else if ((n >> 16) > 0)
	{
		shift = 16;
		high = n >> 16;
		mid = n;
	}
	else
	{
		if (n == 0)
			return 0;

		// Just use the lookup table directly.
		return sqrt_lut[n - 1] >> 24;
	}

	// Linear interpolation:
	// r ~= table_h(high) + table_m(high) * mid / 65536
	// Largely accurate for any values beyond 1, and we never actually run into high == 0
	// because of the above shifts.
	u32 high_val = sqrt_lut[high - 1];
	u32 mid_term = sqrt_lut[high] - high_val;

	u64 mid_calc = static_cast<u64>(mid_term) * mid;
	u32 mid_val = mid_calc >> 16;

	u64 r = (static_cast<u64>(high_val) + mid_val) >> shift;

	// Now none of the above values are 0, they're at least:
	static_assert((isqrt64_slow(1ull << 48) >> 16) >= 255);
	// Two rounds of newton won't put that on 0, so we never divide by 0.
	// Two iterations of Newton's method refine enough that our result is always 1-2 away
	// from the exact value.
	for (int i = 0; i < 2; ++i)
		r = (r + n / r) >> 1;

	// Overflow safety: the highest value for r before newton is 4294967423:
	constexpr u64 max = isqrt64_slow(65535ull << 48);
	constexpr u64 m2 = max - isqrt64_slow(65534ull << 48);
	constexpr u64 m3 = max + m2 + 128;
	// That's slightly more than 2^32, but after two round of newton's method
	constexpr u64 m4 = (m3 + -1ull / m3) >> 1;
	constexpr u64 m5 = (m4 + -1ull / m4) >> 1;
	// Then it's exactly 2^32-1, so at this point all values fit in a u32 and we can safely cast.
	static_assert(m5 == 0xFFFFFFFF);

	return static_cast<u32>(r);
}
