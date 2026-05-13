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

#include "StructuredClone.h"

#include "lib/debug.h"
#include "ps/Profile.h"
#include "scriptinterface/Exceptions.h"
#include "scriptinterface/Request.h"

#include <js/CallArgs.h>
#include <js/RootingAPI.h>
#include <js/StructuredClone.h>

namespace Script { class Interface; }

Script::StructuredClone Script::WriteStructuredClone(const Script::Request& rq, JS::HandleValue v)
{
	Script::StructuredClone ret(new JSStructuredCloneData(JS::StructuredCloneScope::SameProcess));
	JS::CloneDataPolicy policy;
	if (!JS_WriteStructuredClone(rq.cx, v, ret.get(), JS::StructuredCloneScope::SameProcess, policy, nullptr, nullptr, JS::UndefinedHandleValue))
	{
		debug_warn(L"Writing a structured clone with JS_WriteStructuredClone failed!");
		Script::Exception::CatchPending(rq);
		return StructuredClone();
	}

	return ret;
}

void Script::ReadStructuredClone(const Script::Request& rq, const Script::StructuredClone& ptr, JS::MutableHandleValue ret)
{
	JS::CloneDataPolicy policy;
	if (!JS_ReadStructuredClone(rq.cx, *ptr, JS_STRUCTURED_CLONE_VERSION, ptr->scope(), ret, policy, nullptr, nullptr))
		Script::Exception::CatchPending(rq);
}

JS::Value Script::CloneValueFromOtherCompartment(const Script::Interface& to, const Script::Interface& from, JS::HandleValue val)
{
	PROFILE("CloneValueFromOtherCompartment");
	Script::StructuredClone structuredClone;
	{
		Script::Request fromRq(from);
		structuredClone = WriteStructuredClone(fromRq, val);
	}
	Script::Request toRq(to);
	JS::RootedValue out(toRq.cx);
	ReadStructuredClone(toRq, structuredClone, &out);
	return out.get();
}

JS::Value Script::DeepCopy(const Script::Request& rq, JS::HandleValue val)
{
	JS::RootedValue out(rq.cx);
	ReadStructuredClone(rq, WriteStructuredClone(rq, val), &out);
	return out.get();
}
