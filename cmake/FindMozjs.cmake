# Find Mozilla Spidermonkey Javascript Engine (mozjs)
# This module provides the following Imported Targets:
#	Mozjs::Mozjs
# These variables may optionally be set to help this module find the correct files:
#	MOZJS_LIBRARY
#	MOZJS_INCLUDE_DIR

find_path(MOZJS_INCLUDE_DIR jsapi.h
	PATHS
		${MOZJS_DIR}
	PATH_SUFFIXES
		mozjs-${Mozjs_FIND_VERSION}
)

# Parse version number from include Header
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(arch_dep_config_file "${MOZJS_INCLUDE_DIR}/js-config-64.h")
else()
	set(arch_dep_config_file "${MOZJS_INCLUDE_DIR}/js-config-32.h")
endif()

file(STRINGS "${MOZJS_INCLUDE_DIR}/js-config.h" version-strings
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
string(REGEX REPLACE "^#define[ \t]+MOZJS_MAJOR_VERSION[ \t]+([0-9]+)" "\\1" MOZJS_MAJOR_VERSION ${major-line})
string(REGEX REPLACE "^#define[ \t]+MOZJS_MINOR_VERSION[ \t]+([0-9]+)" "\\1" MOZJS_MINOR_VERSION ${minor-line})
unset(version-strings)
unset(major-line)
unset(minor-line)
set(Mozjs_VERSION ${MOZJS_MAJOR_VERSION}.${MOZJS_MINOR_VERSION})
mark_as_advanced(config_file)

# Set some default names, as every distro seams to make their own choice.
# mozjs-${Mozjs_FIND_VERSION}.so.0 is required for Fedora/OpenSUSE Tumbleweed/RPM based systems, mozjs${Mozjs_FIND_VERSION}-release for bundled mozjs
set(MOZJS_LIBNAMES mozjs mozjs-${Mozjs_FIND_VERSION} mozjs${Mozjs_FIND_VERSION} mozjs-${Mozjs_FIND_VERSION}.so.0 mozjs${Mozjs_FIND_VERSION}-release)
find_library(MOZJS_LIBRARY
	NAMES
		${MOZJS_LIBNAMES}
	PATHS
		${MOZJS_DIR}
	DOC "Mozilla SpiderMonkey JavaScript Engine Library"
)
find_library(MOZJS_LIBRARY_DEBUG
	NAMES
		mozjs${Mozjs_FIND_VERSION}d
	PATHS
		${MOZJS_DIR}
)
mark_as_advanced(MOZJS_LIBRARY_DEBUG)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mozjs
	REQUIRED_VARS
		MOZJS_LIBRARY
		MOZJS_INCLUDE_DIR
	VERSION_VAR
		Mozjs_VERSION
)

if (Mozjs_FOUND AND NOT TARGET Mozjs::Mozjs)
	add_library(Mozjs::Mozjs UNKNOWN IMPORTED)
	set_target_properties(Mozjs::Mozjs
		PROPERTIES
			IMPORTED_LOCATION ${MOZJS_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${MOZJS_INCLUDE_DIR}
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${MOZJS_LIBRARY}"
			INTERFACE_COMPILE_DEFINITIONS $<$<CONFIG:DEBUG>:MOZ_DIAGNOSTIC_ASSERT_ENABLED>
	)
	set_property(TARGET Mozjs::Mozjs APPEND PROPERTY
		INTERFACE_COMPILE_DEFINITIONS $<$<PLATFORM_ID:Windows>:XP_WIN>
	)
	set_property(TARGET Mozjs::Mozjs APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	if(EXISTS "${MOZJS_LIBRARY_DEBUG}")
		set_property(TARGET Mozjs::Mozjs APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(Mozjs::Mozjs
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${MOZJS_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
