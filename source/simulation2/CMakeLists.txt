add_library(simulation2 STATIC)

target_sources(simulation2
	PRIVATE
		MessageTypes.h
		Simulation2.cpp
		Simulation2.h
		TypeList.h
)

target_include_directories(simulation2
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)

disable_rtti(simulation2)

if(NOT without-pch)
	add_pch(TARGET simulation2 PCH_DIR ${PROJECT_SOURCE_DIR}/pch/simulation2)
else()
	target_compile_definitions(simulation2 PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(simulation2
		PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
	target_include_directories(simulation2
		BEFORE PRIVATE
			${PROJECT_SOURCE_DIR}/pch/simulation2
	)
endif()

target_link_libraries(simulation2
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		fmt::fmt
		LibXml2::LibXml2
		Iconv::Iconv
)

add_subdirectory(components/)
add_subdirectory(helpers/)
add_subdirectory(scripting/)
add_subdirectory(serialization/)
add_subdirectory(system/)
if(NOT without-tests)
	add_subdirectory(tests/)
endif()
