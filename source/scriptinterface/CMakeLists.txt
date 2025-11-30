add_library(scriptinterface STATIC)

target_sources(scriptinterface
	PRIVATE
		FunctionWrapper.h
		JSON.cpp
		JSON.h
		ModuleLoader.cpp
		ModuleLoader.h
		Object.h
		Promises.cpp
		Promises.h
		Context.cpp
		Context.h
		Conversions.cpp
		Conversions.h
		Engine.h
		Exceptions.cpp
		Exceptions.h
		ForwardDeclarations.h
		Interface.cpp
		Interface.h
		Request.h
		Stats.cpp
		Stats.h
		StructuredClone.cpp
		StructuredClone.h
)

target_include_directories(scriptinterface
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)

target_link_libraries(scriptinterface
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		$<$<AND:$<BOOL:${with-valgrind}>,$<PLATFORM_ID:Linux,Darwin,FreeBSD>>:Valgrind::Valgrind>
		SDL2::SDL2
		fmt::fmt
)

target_compile_definitions(scriptinterface
	PRIVATE
		$<$<AND:$<BOOL:${with-valgrind}>,$<PLATFORM_ID:Linux,Darwin,FreeBSD>>:CONFIG2_VALGRIND>
)

disable_rtti(scriptinterface)

if(NOT without-pch)
	add_pch(TARGET scriptinterface PCH_DIR ${PROJECT_SOURCE_DIR}/pch/scriptinterface)
else()
	target_compile_definitions(scriptinterface PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(scriptinterface
		PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
	target_include_directories(scriptinterface
		BEFORE PRIVATE
			${PROJECT_SOURCE_DIR}/pch/scriptinterface
	)
endif()

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
