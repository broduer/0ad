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

#include "lib/self_test.h"

#include "lib/types.h"
#include "maths/Sqrt.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>

class TestSqrt : public CxxTest::TestSuite
{
public:
	void t(u32 n)
	{
		TS_ASSERT_EQUALS(isqrt64(static_cast<u64>(n) * static_cast<u64>(n)), n);
	}

	void s(u64 n, u64 exp)
	{
		TS_ASSERT_EQUALS(static_cast<u32>(isqrt64(n)), static_cast<u32>(exp));
	}

	void test_sqrt()
	{
		t(0);
		t(1);
		t(2);
		t(255);
		t(256);
		t(257);
		t(65535);
		t(65536);
		t(65537);
		t(16777215);
		t(16777216);
		t(16777217);
		t(2147483647);
		t(2147483648u);
		t(2147483649u);
		t(4294967295u);

		s(2, 1);
		s(3, 1);
		s(4, 2);
		s(255, 15);
		s(256, 16);
		s(257, 16);
		s(65535, 255);
		s(65536, 256);
		s(65537, 256);
		s(999999, 999);
		s(1000000, 1000);
		s(1000001, 1000);
		s(static_cast<u64>(-1), 4294967295u);
	}

	void test_high()
	{
		// Sample the high range and compare against sqrt(double).
		// Known off-by-one cases are acceptable for this approximation.
		for (u64 i = 0; i < 65536; i += 4)
		{
			u64 n = static_cast<u64>(-1) - (1ull << 48) - (1ull << 32) * i;
			s(n, static_cast<u64>(std::sqrt(static_cast<double>(n))));
			n = static_cast<u64>(-1) - (1ull << 49) - (1ull << 32) * i;
			s(n, static_cast<u64>(std::sqrt(static_cast<double>(n))));
			n = static_cast<u64>(-1) - (1ull << 50) - (1ull << 32) * i;
			s(n, static_cast<u64>(std::sqrt(static_cast<double>(n))));
		}
	}

	void test_low()
	{
		// Test values in the range [0, 2^16)
		for (u64 i = 0; i < 65536; ++i)
			s(i, static_cast<u64>(std::sqrt(static_cast<double>(i))));
	}

	void test_random()
	{
		// Test with random u64s to keep the approximation close to floor(sqrt(double))

		std::mt19937 engine(42);

		std::uniform_int_distribution<u64> distributions[4] = {
			std::uniform_int_distribution<u64>(2000ul*2000*65535*65535, -1ull),
			std::uniform_int_distribution<u64>(20ul*20*65535*65535, 2000ul*2000*65535*65535),
			std::uniform_int_distribution<u64>(65535, 20ul*20*65535*65535),
			std::uniform_int_distribution<u64>(0, 65535),
		};

		for (auto& ints : distributions)
		{
			// Focus on value ranges around common gameplay distance squares.

			double error = .0;
			double error_max = .0;

			size_t n = 1024;
			for (size_t i = 0; i < n; ++i)
			{
				u64 n = ints(engine);
				u32 isqrt = isqrt64(n);
				u64 sqrt = std::sqrt(static_cast<double>(n));
				i64 signedDiff = static_cast<i64>(isqrt) - static_cast<i64>(sqrt);
				u64 diff = static_cast<u64>(std::abs(signedDiff));
				error += diff;
				error_max = std::max(error_max, static_cast<double>(diff));
			}
			TS_ASSERT_LESS_THAN(error / n, 1.f);
			TS_ASSERT_LESS_THAN(error_max, 1.1f);
		}
	}
};
