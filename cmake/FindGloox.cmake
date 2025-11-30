# Locate Gloox
# This module provides the following Imported Targets:
#	Gloox::Gloox
# These variables may optionally be set to help this module find the correct files:
#	GLOOX_LIBRARY
#	GLOOX_INCLUDE_DIR

if(NOT GLOOX_INCLUDE_DIR)
	find_path(GLOOX_INCLUDE_DIR gloox/gloox.h
		PATHS
			${GLOOX_DIR}
	)

	# Parse version number from include Header
	file(STRINGS "${GLOOX_INCLUDE_DIR}/gloox/glooxversion.h" version-strings
		REGEX "#define[ \t]+GLOOXVERSION.*"
	)
	list(GET version-strings 0 version-line)
	# CMake does not support {n}
	string(REGEX MATCHALL "([0-9][0-9])" gloox_version_number ${version-line})
	list(GET gloox_version_number 0 Gloox_VERSION_MAJOR)
	list(GET gloox_version_number 1 Gloox_VERSION_MINOR)
	list(GET gloox_version_number 2 Gloox_VERSION_PATCH)
	set(Gloox_VERSION ${Gloox_VERSION_MAJOR}.${Gloox_VERSION_MINOR}.${Gloox_VERSION_PATCH})
	unset(version-strings)
	unset(version-line)
	unset(gloox_version_number)
endif()

if(NOT GLOOX_LIBRARY)
	find_library(GLOOX_LIBRARY
		NAMES
			gloox gloox-1.0
		PATHS
			${GLOOX_DIR}
		DOC "gloox – The Portable XMPP Engine"
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(GLOOX_LIBRARY_DEBUG
			NAMES
				glooxd gloox-1.0d
			PATHS
				${GLOOX_DIR}
		)
		mark_as_advanced(GLOOX_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Gloox
	REQUIRED_VARS
		GLOOX_LIBRARY
		GLOOX_INCLUDE_DIR
	VERSION_VAR
		Gloox_VERSION
)

if(Gloox_FOUND AND NOT TARGET Gloox::Gloox)
	add_library(Gloox::Gloox UNKNOWN IMPORTED)
	set_target_properties(Gloox::Gloox
		PROPERTIES
			IMPORTED_LOCATION ${GLOOX_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${GLOOX_INCLUDE_DIR}
	)
	set_property(TARGET Gloox::Gloox APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	set_target_properties(Gloox::Gloox
		PROPERTIES
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${GLOOX_LIBRARY}"
	)
	if(EXISTS "${GLOOX_LIBRARY_DEBUG}")
		set_property(TARGET Gloox::Gloox APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(Gloox::Gloox
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${GLOOX_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
