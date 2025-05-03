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

#ifndef INCLUDED_MATH_SQRT
#define INCLUDED_MATH_SQRT

#include "lib/types.h"

/**
 * Fast approximate 64-bit integer square root.
 * Returns an estimate for sqrt(n) over the complete u64 range.
 * The current implementation is expected to stay within about 1 of floor(sqrt(n)).
 */
u32 isqrt64(u64 n);

#endif // INCLUDED_MATH_SQRT
