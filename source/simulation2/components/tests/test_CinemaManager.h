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

#include "maths/Fixed.h"
#include "maths/FixedVector3D.h"
#include "maths/NUSpline.h"
#include "ps/CStr.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/Interface.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpCinemaManager.h"
#include "simulation2/helpers/CinemaPath.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/ComponentTest.h"
#include "simulation2/system/Entity.h"

#include <cstddef>
#include <memory>
#include <string>

class TestCmpCinemaManager : public CxxTest::TestSuite
{
public:
	void test_managing_paths()
	{
		CXeromycesEngine xeromycesEngine;
		ComponentTestHelper test(*g_ScriptContext);

		ICmpCinemaManager* cmp = test.Add<ICmpCinemaManager>(CID_CinemaManager, "", SYSTEM_ENTITY);

		TS_ASSERT(!cmp->HasPath(L"test"));
		cmp->AddPath(generatePath(L"test"));
		TS_ASSERT(cmp->HasPath(L"test"));
		TS_ASSERT(!cmp->HasPath(L"test_2"));
		cmp->SetPaths(std::map<CStrW, CCinemaPath>{{L"test_2", generatePath(L"test_2")}});
		TS_ASSERT(!cmp->HasPath(L"test"));
		TS_ASSERT(cmp->HasPath(L"test_2"));
		cmp->DeletePath(L"test_2");
		TS_ASSERT(!cmp->HasPath(L"test_2"));
	}

	void test_playing_queue()
	{
		CXeromycesEngine xeromycesEngine;
		ComponentTestHelper test(*g_ScriptContext);

		ICmpCinemaManager* cmp = test.Add<ICmpCinemaManager>(CID_CinemaManager, "", SYSTEM_ENTITY);

		CMessageUpdate updateMsg(fixed::FromInt(200));

		cmp->AddPath(generatePath(L"path_1", fixed::FromInt(10000)));
		cmp->AddPath(generatePath(L"path_2", fixed::FromInt(5000)));

		// Try getting the active path if there is none.
		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"");

		// Try to start playing the queue if it's empty.
		cmp->StartPlayingQueue();
		TS_ASSERT(!cmp->IsPlayingQueue());

		// Try stopping playing the queue if it's not playing in the first place.
		cmp->StartPlayingQueue();
		TS_ASSERT(!cmp->IsPlayingQueue());

		cmp->PushPathToQueue(L"path_1");
		cmp->PushPathToQueue(L"path_2");
		// Try getting the active path if there is none.
		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"");

		cmp->StartPlayingQueue();
		TS_ASSERT(cmp->IsPlayingQueue());
		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"path_1");
		TS_ASSERT_EQUALS(cmp->GetActivePathElapsedTime(), fixed::FromInt(0));

		for (int i = 0; i < 35; i++)
			cmp->HandleMessage(updateMsg, true);
		TS_ASSERT(cmp->IsPlayingQueue());
		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"path_1");
		TS_ASSERT_EQUALS(cmp->GetActivePathElapsedTime(), fixed::FromInt(7000));

		// Finish path_1 and start with path_2
		for (int i = 0; i < 20; i++)
			cmp->HandleMessage(updateMsg, true);

		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"path_2");
		TS_ASSERT_EQUALS(cmp->GetActivePathElapsedTime(), fixed::FromInt(1000));

		// Try restarting while a path is being played.
		// This should result in the active path starting again from the beginning.
		cmp->StopPlayingQueue();
		cmp->StartPlayingQueue();
		TS_ASSERT(cmp->IsPlayingQueue());
		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"path_2");

		size_t remainingTurns = 0;
		while (cmp->IsPlayingQueue())
		{
			cmp->HandleMessage(updateMsg, true);
			remainingTurns++;
		}
		TS_ASSERT_EQUALS(remainingTurns, 25);
		TS_ASSERT(!cmp->IsPlayingQueue());
		TS_ASSERT_WSTR_EQUALS(cmp->GetActivePath(), L"");
		TS_ASSERT_EQUALS(cmp->GetActivePathElapsedTime(), fixed::FromInt(0));

		// Make sure the queue is empty.
		cmp->StartPlayingQueue();
		TS_ASSERT(!cmp->IsPlayingQueue());
	}

private:
	// Generates a simple cinema path with two position and two target nodes.
	CCinemaPath generatePath(const CStrW& name, const fixed& duration = fixed::FromInt(1))
	{
		// Helper nodes
		CFixedVector3D nodeA(fixed::FromInt(1), fixed::FromInt(0), fixed::FromInt(1));
		CFixedVector3D nodeB(fixed::FromInt(9), fixed::FromInt(0), fixed::FromInt(9));
		CFixedVector3D shift(fixed::FromInt(3), fixed::FromInt(3), fixed::FromInt(3));

		// Constructs the default cinema path data
		CCinemaData pathData;
		pathData.m_Name = name;
		pathData.m_Timescale = fixed::FromInt(1);
		pathData.m_Orientation = L"target";
		pathData.m_Mode = L"ease_inout";
		pathData.m_Style = L"default";

		// Creates two parallel segments from the A node to the B node
		TNSpline positionSpline, targetSpline;
		positionSpline.AddNode(nodeA, CFixedVector3D(), fixed::FromInt(0));
		positionSpline.AddNode(nodeB, CFixedVector3D(), duration);
		targetSpline.AddNode(nodeA + shift, CFixedVector3D(), fixed::FromInt(0));
		targetSpline.AddNode(nodeB + shift, CFixedVector3D(), duration);

		return CCinemaPath(pathData, positionSpline, targetSpline);
	}
};
