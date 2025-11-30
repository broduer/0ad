add_library(tinygettext STATIC)

if(NOT without-pch)
	add_pch(TARGET tinygettext PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/tinygettext)
else()
	target_compile_definitions(tinygettext PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(tinygettext PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

target_include_directories(tinygettext
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
	PUBLIC
		${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(tinygettext
	PRIVATE
		${BUILD_FLAGS_TARGET}
		Iconv::Iconv
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		fmt::fmt
)

target_compile_options(tinygettext
	PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:/wd4127>
		$<$<CXX_COMPILER_ID:MSVC>:/wd4309>
		$<$<CXX_COMPILER_ID:MSVC>:/wd4800>
		$<$<CXX_COMPILER_ID:MSVC>:/wd4100>
		$<$<CXX_COMPILER_ID:MSVC>:/wd4996>
		$<$<CXX_COMPILER_ID:MSVC>:/wd4099>
		$<$<CXX_COMPILER_ID:MSVC>:/wd4503>
)

disable_rtti(tinygettext)

add_subdirectory(src/)
