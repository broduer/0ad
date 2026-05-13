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

#include "lib/config2.h"
#if CONFIG2_LOBBY
#include "lib/external_libraries/gloox.h"
#include "lib/utf8.h"
#include "lobby/GlooxConversion.h"
#include "scriptinterface/Conversions.h"

#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <string>

namespace Script { class Request; }

template<> void Script::ToJSVal<gloox::Presence::PresenceType>(const Script::Request& rq, JS::MutableHandleValue ret, const gloox::Presence::PresenceType& val)
{
	ToJSVal(rq, ret, GetPresenceString(val));
}

template<> void Script::ToJSVal<gloox::MUCRoomRole>(const Script::Request& rq, JS::MutableHandleValue ret, const gloox::MUCRoomRole& val)
{
	ToJSVal(rq, ret, GetRoleString(val));
}

template<> void Script::ToJSVal<gloox::StanzaError>(const Script::Request& rq, JS::MutableHandleValue ret, const gloox::StanzaError& val)
{
	ToJSVal(rq, ret, wstring_from_utf8(StanzaErrorToString(val)));
}

template<> void Script::ToJSVal<gloox::ConnectionError>(const Script::Request& rq, JS::MutableHandleValue ret, const gloox::ConnectionError& val)
{
	ToJSVal(rq, ret, wstring_from_utf8(ConnectionErrorToString(val)));
}

template<> void Script::ToJSVal<gloox::RegistrationResult>(const Script::Request& rq, JS::MutableHandleValue ret, const gloox::RegistrationResult& val)
{
	ToJSVal(rq, ret, wstring_from_utf8(RegistrationResultToString(val)));
}

template<> void Script::ToJSVal<gloox::CertStatus>(const Script::Request& rq, JS::MutableHandleValue ret, const gloox::CertStatus& val)
{
	ToJSVal(rq, ret, wstring_from_utf8(CertificateErrorToString(val)));
}

#endif // CONFIG2_LOBBY
