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

#include "HeightmapArray.h"
#include "MapEditor_Brushes.h"
#include "ps/TemplateLoader.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Conversions.h"
#include "TerrainArray.h"

using namespace JSI_MapEditor;

template<> void Script::ToJSVal<Brush*>(const Script::Request& rq, JS::MutableHandleValue ret, Brush* const& val)
{
	if (val == nullptr)
	{
		ret.setNull();
		return;
	}

	JS::RootedObject obj{rq.cx, rq.GetScriptInterface().CreateCustomObject("Brush")};
	JS::SetReservedSlot(obj, Script::Interface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(static_cast<void*>(val)));

	ret.setObject(*obj);
}

template<> bool Script::FromJSVal<const Brush*>([[maybe_unused]] const Script::Request& rq, JS::HandleValue val, const Brush*& out)
{
	if (!val.isObject())
		return false;

	out = JS::GetMaybePtrFromReservedSlot<const Brush>(&val.toObject(), Script::Interface::JSObjectReservedSlots::PRIVATE);
	return true;
}

template<> bool Script::FromJSVal<std::vector<float>>(const Script::Request& rq, JS::HandleValue val, std::vector<float>& out)
{
	return Script::FromJSVal_vector(rq, val, out);
}

template<> void Script::ToJSVal<TerrainTileArray*>(const Script::Request& rq, JS::MutableHandleValue ret, TerrainTileArray* const& val)
{
	if (val == nullptr)
	{
		ret.setNull();
		return;
	}

	JS::RootedObject obj{rq.cx, rq.GetScriptInterface().CreateCustomObject("TerrainTileArray")};
	JS::SetReservedSlot(obj, Script::Interface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(static_cast<void*>(val)));

	ret.setObject(*obj);
}

template<> bool Script::FromJSVal<TerrainTileArray>(const Script::Request& rq, JS::HandleValue val, TerrainTileArray& out)
{
	if (!val.isObject())
	{
		Script::Exception::Raise(rq, "Value is not an TerrainTileArray.");
		return false;
	}

	JS::RootedObject obj{rq.cx, &val.toObject()};
	out = *(JS::GetMaybePtrFromReservedSlot<TerrainTileArray>(obj, Script::Interface::JSObjectReservedSlots::PRIVATE));
	return true;
}

template<> void Script::ToJSVal<HeightmapArray*>(const Script::Request& rq, JS::MutableHandleValue ret, HeightmapArray* const& val)
{
	if (val == nullptr)
	{
		ret.setNull();
		return;
	}

	JS::RootedObject obj{rq.cx, rq.GetScriptInterface().CreateCustomObject("HeightmapArray")};
	JS::SetReservedSlot(obj, Script::Interface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(static_cast<void*>(val)));

	ret.setObject(*obj);
}

template<> bool Script::FromJSVal<HeightmapArray>(const Script::Request& rq, JS::HandleValue val, HeightmapArray& out)
{
	if (!val.isObject())
	{
		Script::Exception::Raise(rq, "Value is not an HeightmapArray.");
		return false;
	}

	JS::RootedObject obj{rq.cx, &val.toObject()};
	out = *(JS::GetMaybePtrFromReservedSlot<HeightmapArray>(obj, Script::Interface::JSObjectReservedSlots::PRIVATE));
	return true;
}

template<> bool Script::FromJSVal<ETemplatesType>(const Script::Request& rq, JS::HandleValue val, ETemplatesType& out)
{
	if (!val.isString())
	{
		Script::Exception::Raise(rq, "Value is not a string.");
		return false;
	}

	std::string str;
	Script::FromJSVal(rq, val, str);
	if (str == "actor")
		out = ETemplatesType::ACTOR_TEMPLATES;
	else if (str == "simulation")
		out = ETemplatesType::SIMULATION_TEMPLATES;
	else if (str == "all")
		out = ETemplatesType::ALL_TEMPLATES;
	else
	{
		Script::Exception::Raise(rq, "Invalid template type.");
		return false;
	}

	return true;
}
