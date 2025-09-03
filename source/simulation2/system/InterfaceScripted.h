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

#ifndef INCLUDED_INTERFACE_SCRIPTED
#define INCLUDED_INTERFACE_SCRIPTED

#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Conversions.h"
#include "scriptinterface/Interface.h"
#include "simulation2/system/ComponentManager.h"

#define BEGIN_INTERFACE_WRAPPER(iname) \
	JSClass class_ICmp##iname = { \
		"ICmp" #iname, JSCLASS_HAS_RESERVED_SLOTS( Script::Interface::JSObjectReservedSlots::PRIVATE+1 ) \
	}; \
	static JSFunctionSpec methods_ICmp##iname[] = {

#define END_INTERFACE_WRAPPER(iname) \
		JS_FS_END \
	}; \
	void ICmp##iname::InterfaceInit(Script::Interface& scriptInterface) { \
		scriptInterface.DefineCustomObjectType(&class_ICmp##iname, NULL, 0, NULL, methods_ICmp##iname, NULL, NULL); \
	} \
	bool ICmp##iname::NewJSObject(const Script::Interface& scriptInterface, JS::MutableHandleObject out) const\
	{ \
		out.set(scriptInterface.CreateCustomObject("ICmp" #iname)); \
		IComponent* comp = const_cast<IComponent*>(static_cast<const IComponent*>(this)); \
		JS::SetReservedSlot(out, Script::Interface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(comp)); \
		return true; \
	} \
	JS::HandleValue ICmp##iname::GetJSInstance() const \
	{ \
		if (m_CachedInstance) \
			return JS::HandleValue::fromMarkedLocation(m_CachedInstance.unsafeAddress()); \
		\
		const Script::Interface& si = GetSimContext().GetScriptInterface(); \
		Script::Request rq(si); \
		JS::RootedObject obj(rq.cx); \
		NewJSObject(GetSimContext().GetScriptInterface(), &obj); \
		m_CachedInstance.setObject(*obj); \
		\
		GetSimContext().GetComponentManager().RegisterTrace(GetEntityId(), m_CachedInstance); \
		return JS::HandleValue::fromMarkedLocation(m_CachedInstance.unsafeAddress()); \
	} \
	void RegisterComponentInterface_##iname(Script::Interface& scriptInterface) { \
		ICmp##iname::InterfaceInit(scriptInterface); \
	}

template <typename T>
inline T* ComponentGetter(const Script::Request& rq, JS::CallArgs& args)
{
	return Script::Interface::GetPrivate<T>(rq, args);
}

#define DEFINE_INTERFACE_METHOD(scriptname, classname, methodname) \
	Script::Function::Wrap<&classname::methodname, ComponentGetter<classname>>(scriptname),

#endif // INCLUDED_INTERFACE_SCRIPTED
