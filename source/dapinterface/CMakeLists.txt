add_library(dapinterface STATIC)

target_sources(dapinterface
	PRIVATE
		DapInterface.cpp
		DapInterface.h
)

target_link_libraries(dapinterface
	PRIVATE
	${BUILD_FLAGS_TARGET}
	$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
	$<$<PLATFORM_ID:Windows>:BOOST::headers>
	fmt::fmt
	$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
)

target_include_directories(dapinterface
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)

disable_rtti(dapinterface)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
