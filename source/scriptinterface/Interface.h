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

#ifndef INCLUDED_SCRIPT_INTERFACE
#define INCLUDED_SCRIPT_INTERFACE

#include "lib/code_annotation.h"
#include "lib/types.h"
#include "ps/Errors.h"
#include "scriptinterface/Conversions.h"
#include "scriptinterface/Exceptions.h"
#include "scriptinterface/Request.h"

#include <functional>
#include <js/CallArgs.h>
#include <js/Object.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

class JSObject;
class Path;
namespace Script { class Context; }
namespace Script { class ModuleLoader; }
namespace Script { struct Interface_impl; }
struct JSClass;
struct JSContext;
struct JSFunctionSpec;
struct JSPropertySpec;


ERROR_GROUP(Scripting);
ERROR_TYPE(Scripting, SetupFailed);

ERROR_SUBGROUP(Scripting, LoadFile);
ERROR_TYPE(Scripting_LoadFile, OpenFailed);
ERROR_TYPE(Scripting_LoadFile, EvalErrors);

ERROR_TYPE(Scripting, CallFunctionFailed);
ERROR_TYPE(Scripting, DefineConstantFailed);
ERROR_TYPE(Scripting, CreateObjectFailed);
ERROR_TYPE(Scripting, TypeDoesNotExist);

ERROR_SUBGROUP(Scripting, DefineType);
ERROR_TYPE(Scripting_DefineType, AlreadyExists);
ERROR_TYPE(Scripting_DefineType, CreationFailed);

// Set the maximum number of function arguments that can be handled
// (This should be as small as possible (for compiler efficiency),
// but as large as necessary for all wrapped functions)
#define SCRIPT_INTERFACE_MAX_ARGS 8

using VfsPath = Path;

// Using a global object for the context is a workaround until Simulation, AI, etc,
// use their own threads and also their own contexts.
extern thread_local std::shared_ptr<Script::Context> g_ScriptContext;

/**
 * Abstraction around a SpiderMonkey JS::Realm.
 *
 * Thread-safety:
 * - May be used in non-main threads.
 * - Each Script::Interface must be created, used, and destroyed, all in a single thread
 *   (it must never be shared between threads).
 */

namespace Script
{

class Interface
{
	NONCOPYABLE(Interface);

	friend class Request;

public:

	/**
	 * Constructor.
	 * @param nativeScopeName Name of global object that functions (via Script::Function::Register) will
	 *   be placed into, as a scoping mechanism; typically "Engine"
	 * @param debugName Name of this interface for CScriptStats purposes.
	 * @param context ScriptContext to use when initializing this interface.
	 */
	Interface(const char* nativeScopeName, const char* debugName, Context& context,
		std::function<bool(const VfsPath&)> allowModule = {});

	template<typename Context>
	Interface(const char* nativeScopeName, const char* debugName, Context&& context,
		std::function<bool(const VfsPath&)> allowModule = {}) :
		Interface{nativeScopeName, debugName, *context, std::move(allowModule)}
	{
		static_assert(std::is_lvalue_reference_v<Context>, "`Interface` doesn't take ownership "
			"of the context.");
	}

	/**
	 * Alternate constructor. This creates the new Realm in the same Compartment as the neighbor scriptInterface.
	 * This means that data can be freely exchanged between these two script interfaces without cloning.
	 * @param nativeScopeName Name of global object that functions (via Script::Function::Register) will
	 *   be placed into, as a scoping mechanism; typically "Engine"
	 * @param debugName Name of this interface for CScriptStats purposes.
	 * @param scriptInterface 'Neighbor' scriptInterface to share a compartment with.
	 * @param allowModule A predicate deciding wheter to import the given
	 *	module.
	 */
	Interface(const char* nativeScopeName, const char* debugName, const Interface& neighbor,
		std::function<bool(const VfsPath&)> allowModule = {});

	~Interface();

	struct CmptPrivate
	{
		friend class Interface;
	public:
		static const Interface& GetScriptInterface(JSContext* cx);
		static void* GetCBData(JSContext* cx);
	protected:
		Interface* pScriptInterface; // the Interface object the compartment belongs to
		void* pCBData; // meant to be used as the "this" object for callback functions
	};

	void SetCallbackData(void* pCBData);

	/**
	 * Convert the CmptPrivate callback data to T*
	 */
	template <typename T>
	static T* ObjectFromCBData(const Request& rq)
	{
		static_assert(!std::is_same_v<void, T>);
		return static_cast<T*>(ObjectFromCBData<void>(rq));
	}

	/**
	 * Variant for the function wrapper.
	 */
	template <typename T>
	static T* ObjectFromCBData(const Request& rq, JS::CallArgs&)
	{
		return ObjectFromCBData<T>(rq);
	}

	ModuleLoader& GetModuleLoader() const;

	/**
	 * GetGeneralJSContext returns the context without starting a GC request and without
	 * entering the Interface compartment. It should only be used in specific situations,
	 * for instance when initializing a persistent rooted.
	 * If you need the compartmented context of the Interface, you should create a
	 * Interface::Request and use the context from that.
	 */
	JSContext* GetGeneralJSContext() const;
	Context& GetContext() const;

	/**
	 * Load global scripts that most script interfaces need,
	 * located in the /globalscripts directory. VFS must be initialized.
	 */
	bool LoadGlobalScripts();

	/**
	 * Replace the default JS random number generator with a seeded, network-synced one.
	 */
	bool ReplaceNondeterministicRNG(std::mt19937_64& rng);

	/**
	 * Call a constructor function, equivalent to JS "new ctor(arg)".
	 * @param ctor An object that can be used as constructor
	 * @param argv Constructor arguments
	 * @param out The new object; On error an error message gets logged and out is Null (out.isNull() == true).
	 */
	void CallConstructor(JS::HandleValue ctor, JS::HandleValueArray argv, JS::MutableHandleValue out) const;

	JSObject* CreateCustomObject(const std::string & typeName) const;
	void DefineCustomObjectType(JSClass *clasp, JSNative constructor, uint minArgs, JSPropertySpec *ps, JSFunctionSpec *fs, JSPropertySpec *static_ps, JSFunctionSpec *static_fs);

	/**
	 * Set the named property on the global object.
	 * Optionally makes it {ReadOnly, DontEnum}. We do not allow to make it DontDelete, so that it can be hotloaded
	 * by deleting it and re-creating it, which is done by setting @p replace to true.
	 */
	template<typename T>
	bool SetGlobal(const char* name, const T& value, bool replace = false, bool constant = true, bool enumerate = true);

	/**
	 * Get an object from the global scope or any lexical scope.
	 * This can return globally accessible objects even if they are not properties
	 * of the global object (e.g. ES6 class definitions).
	 * @param name - Name of the property.
	 * @param out The object or null.
	 */
	static bool GetGlobalProperty(const Request& rq, const std::string& name, JS::MutableHandleValue out);

	bool SetPrototype(JS::HandleValue obj, JS::HandleValue proto);

	/**
	 * Load and execute the given script in a new function scope.
	 * @param filename Name for debugging purposes (not used to load the file)
	 * @param code JS code to execute
	 * @return true on successful compilation and execution; false otherwise
	 */
	bool LoadScript(const VfsPath& filename, const std::string& code) const;

	/**
	 * Load and execute the given script in the global scope.
	 * @param filename Name for debugging purposes (not used to load the file)
	 * @param code JS code to execute
	 * @return true on successful compilation and execution; false otherwise
	 */
	bool LoadGlobalScript(const VfsPath& filename, const std::string& code) const;

	/**
	 * Load and execute the given script in the global scope.
	 * @return true on successful compilation and execution; false otherwise
	 */
	bool LoadGlobalScriptFile(const VfsPath& path) const;

	/**
	 * Evaluate some JS code in the global scope.
	 * @return true on successful compilation and execution; false otherwise
	 */
	bool Eval(const std::string_view code) const;
	bool Eval(const std::string_view code, JS::MutableHandleValue out) const;
	template<typename T> bool Eval(const std::string_view code, T& out) const;

	/**
	 * Calls the random number generator assigned to this Interface instance and returns the generated number.
	 */
	bool MathRandom(double& nbr) const;

	/**
	 * JSNative wrapper of the above.
	 */
	static bool Math_random(JSContext* cx, uint argc, JS::Value* vp);

	/**
	 * Name the reserved slots we may need to use in custom JSObjects.
	 * When using JSCLASS_HAS_RESERVED_SLOTS in the definition of your JSClass, use the number
	 * of the highest slot you need plus 1.
	 */
	enum JSObjectReservedSlots {
		PRIVATE = 0
	};

	/**
	 * Retrieve the private data field of a JSObject.
	 */
	template <typename T>
	static T* GetPrivate(const Request& rq, JS::HandleObject thisobj)
	{
		T* value = JS::GetMaybePtrFromReservedSlot<T>(thisobj, JSObjectReservedSlots::PRIVATE);

		if (value == nullptr)
			Exception::Raise(rq, "Private data of the given object is null!");

		return value;
	}

	/**
	 * Retrieve the private data field of a JS Object.
	 * If an error occurs, GetPrivate will report it with the according stack.
	 */
	template <typename T>
	static T* GetPrivate(const Request& rq, JS::CallArgs& callArgs)
	{
		if (!callArgs.thisv().isObject())
		{
			Exception::Raise(rq, "Cannot retrieve private JS object data because from a non-object value!");
			return nullptr;
		}

		JS::RootedObject thisObj(rq.cx, &callArgs.thisv().toObject());
		T* value = JS::GetMaybePtrFromReservedSlot<T>(thisObj, JSObjectReservedSlots::PRIVATE);

		if (value == nullptr)
			Exception::Raise(rq, "Private data of the given object is null!");

		return value;
	}

private:
	bool SetGlobal_(const char* name, JS::HandleValue value, bool replace, bool constant, bool enumerate);

	struct CustomType
	{
		JS::PersistentRootedObject m_Prototype;
		JSClass* m_Class;
		JSNative m_Constructor;
	};

	CmptPrivate m_CmptPrivate;

	// Take care to keep this declaration before heap rooted members. Destructors of heap rooted
	// members have to be called before the custom destructor of Interface_impl.
	std::unique_ptr<Interface_impl> m;

	std::unordered_map<std::string, CustomType> m_CustomObjectTypes;
};

} // namespace Script

// Explicitly instantiate void* as that is used for the generic template,
// and we want to define it in the .cpp file.
template <> void* Script::Interface::ObjectFromCBData(const Script::Request& rq);

template<typename T>
bool Script::Interface::SetGlobal(const char* name, const T& value, bool replace, bool constant, bool enumerate)
{
	Script::Request rq(this);
	JS::RootedValue val(rq.cx);
	Script::ToJSVal(rq, &val, value);
	return SetGlobal_(name, val, replace, constant, enumerate);
}

template<typename T>
bool Script::Interface::Eval(const std::string_view code, T& ret) const
{
	Script::Request rq(this);
	JS::RootedValue rval(rq.cx);
	if (!Eval(code, &rval))
		return false;
	return Script::FromJSVal(rq, rval, ret);
}

#endif // INCLUDED_SCRIPT_INTERFACE
