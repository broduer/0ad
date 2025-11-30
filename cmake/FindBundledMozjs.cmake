# Find Mozilla Spidermonkey Javascript Engine (mozjs) bundled version
# This module provides the following Imported Targets:
#	BdlMozjs::BdlMozjs
# These variables may optionally be set to help this module find the correct files:
#	BUNDLED_MOZJS_LIBRARY
#	BUNDLED_MOZJS_INCLUDE_DIR
#	BUNDLED_MOZJS_DIR

if(UNIX)
	set(BUNDLED_MOZJS_SEARCH_PATH ${CMAKE_SOURCE_DIR}/libraries/source/spidermonkey)
else()
	set(BUNDLED_MOZJS_SEARCH_PATH ${0AD_EXT_LIBDIR}/spidermonkey)
endif()
find_path(BUNDLED_MOZJS_INCLUDE_DIR jsapi.h
	PATHS
		${BUNDLED_MOZJS_DIR}
		${BUNDLED_MOZJS_SEARCH_PATH}
	PATH_SUFFIXES
		include-release
	NO_DEFAULT_PATH
)
find_path(BUNDLED_MOZJS_INCLUDE_DIR_DEBUG jsapi.h
	PATHS
		${BUNDLED_MOZJS_DIR}
		${BUNDLED_MOZJS_SEARCH_PATH}
	PATH_SUFFIXES
		include-debug
	NO_DEFAULT_PATH
)

# Parse version number from include Header
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(arch_dep_config_file "${BUNDLED_MOZJS_INCLUDE_DIR}/js-config-64.h")
else()
	set(arch_dep_config_file "${BUNDLED_MOZJS_INCLUDE_DIR}/js-config-32.h")
endif()

file(STRINGS "${BUNDLED_MOZJS_INCLUDE_DIR}/js-config.h" version-strings
	REGEX "#define[ \t]+MOZJS_(MAJOR|MINOR|PATCH)_VERSION.*"
)
# Additionally check arch specific header if js-config does not contain MOZJS version
if(NOT version-strings)
	file(STRINGS ${arch_dep_config_file} version-strings
		REGEX "#define[ \t]+MOZJS_(MAJOR|MINOR|PATCH)_VERSION.*"
	)
endif()

list(GET version-strings 0 major-line)
list(GET version-strings 1 minor-line)
string(REGEX REPLACE "^#define[ \t]+MOZJS_MAJOR_VERSION[ \t]+([0-9]+)" "\\1" BUNDLED_MOZJS_MAJOR_VERSION ${major-line})
string(REGEX REPLACE "^#define[ \t]+MOZJS_MINOR_VERSION[ \t]+([0-9]+)" "\\1" BUNDLED_MOZJS_MINOR_VERSION ${minor-line})
unset(version-strings)
unset(major-line)
unset(minor-line)
set(BundledMozjs_VERSION ${BUNDLED_MOZJS_MAJOR_VERSION}.${BUNDLED_MOZJS_MINOR_VERSION})
mark_as_advanced(config_file)

find_library(BUNDLED_MOZJS_LIBRARY
	NAMES
		mozjs${BundledMozjs_FIND_VERSION}-release
	PATHS
		${BUNDLED_MOZJS_DIR}
		${BUNDLED_MOZJS_SEARCH_PATH}
	PATH_SUFFIXES
		lib/
	DOC "Mozilla SpiderMonkey JavaScript Engine Library(Bundled)"
	NO_DEFAULT_PATH
)
find_library(BUNDLED_MOZJS_LIBRARY_DEBUG
	NAMES
		mozjs${BundledMozjs_FIND_VERSION}-debug
	PATHS
		${BUNDLED_MOZJS_DIR}
		${BUNDLED_MOZJS_SEARCH_PATH}
	PATH_SUFFIXES
		lib/
	NO_DEFAULT_PATH
)
find_library(BUNDLED_MOZJS_LIBRARY_RUST
	NAMES
		mozjs${BundledMozjs_FIND_VERSION}-rust
	PATHS
		${BUNDLED_MOZJS_DIR}
		${BUNDLED_MOZJS_SEARCH_PATH}
	PATH_SUFFIXES
		lib/
	NO_DEFAULT_PATH
)
mark_as_advanced(BUNDLED_MOZJS_LIBRARY_DEBUG BUNDLED_MOZJS_LIBRARY_RUST)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BundledMozjs
	REQUIRED_VARS
		BUNDLED_MOZJS_LIBRARY
		BUNDLED_MOZJS_INCLUDE_DIR
	VERSION_VAR
		BundledMozjs_VERSION
)

if (BundledMozjs_FOUND AND NOT TARGET BdlMozjs::BdlMozjs)
	add_library(BdlMozjs::BdlMozjs UNKNOWN IMPORTED)
	target_link_libraries(BdlMozjs::BdlMozjs
		INTERFACE
			${BUNDLED_MOZJS_LIBRARY_RUST}
	)
	set_target_properties(BdlMozjs::BdlMozjs
		PROPERTIES
			IMPORTED_LOCATION ${BUNDLED_MOZJS_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES $<IF:$<CONFIG:Debug>,${BUNDLED_MOZJS_INCLUDE_DIR_DEBUG},${BUNDLED_MOZJS_INCLUDE_DIR}>
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${BUNDLED_MOZJS_LIBRARY}"
			INTERFACE_COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:MOZ_DIAGNOSTIC_ASSERT_ENABLED>
	)
	set_property(TARGET BdlMozjs::BdlMozjs APPEND PROPERTY
		INTERFACE_COMPILE_DEFINITIONS $<$<PLATFORM_ID:Windows>:XP_WIN>
	)
	set_property(TARGET BdlMozjs::BdlMozjs APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	if(EXISTS "${BUNDLED_MOZJS_LIBRARY_DEBUG}")
		set_property(TARGET BdlMozjs::BdlMozjs APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(BdlMozjs::BdlMozjs
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${BUNDLED_MOZJS_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
