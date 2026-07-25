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

#include "ps/Views.h"

#include <vector>

class TestViews : public CxxTest::TestSuite
{
public:
	void test_Enumerate()
	{
		const std::vector<int> v1{0, 1, 2};
		for (auto [i, elem] : PS::Views::Enumerate(v1))
		{
			TS_ASSERT_EQUALS(i, elem);
		}

		std::vector<int> v2{-2, -1, 0};
		for (auto const [i, elem] : v2 | PS::Views::Enumerate(-2))
		{
			TS_ASSERT_EQUALS(i, elem);
		}

		std::vector<int> v3{1, 2, 3};
		const auto ev = v3 | PS::Views::Enumerate(1);
		for (auto [i, elem] : ev) {
			TS_ASSERT_EQUALS(i, elem);
		}
	}
};
