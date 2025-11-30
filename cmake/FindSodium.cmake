# Locate Sodium
# This module provides the following Imported Targets:
#	Sodium::Sodium
# These variables may optionally be set to help this module find the correct files:
#	SODIUM_LIBRARY
#	SODIUM_INCLUDE_DIR

if(NOT SODIUM_INCLUDE_DIR)
	find_path(SODIUM_INCLUDE_DIR sodium.h
		PATHS
			${SODIUM_DIR}
	)

	# Parse version number from include Header
	file(STRINGS "${SODIUM_INCLUDE_DIR}/sodium/version.h" version-strings
		REGEX "#define[ \t]+SODIUM_VERSION_STRING.*"
	)
	list(GET version-strings 0 version-line)
	string(REGEX MATCH "([0-9]+\\.)?([0-9]+\\.)?(\\*|[0-9]+)" Sodium_VERSION ${version-line})
	unset(version-line)
	file(STRINGS "${SODIUM_INCLUDE_DIR}/sodium/version.h" version-strings
		REGEX "#define[ \t]+SODIUM_LIBRARY_VERSION_(MAJOR|MINOR).*"
	)
	list(GET version-strings 0 major-line)
	list(GET version-strings 1 minor-line)
	string(REGEX REPLACE "^#define[ \t]+ENET_VERSION_MAJOR[ \t]+([0-9]+)" "\\1" Sodium_VERSION_MAJOR ${major-line})
	string(REGEX REPLACE "^#define[ \t]+ENET_VERSION_MINOR[ \t]+([0-9]+)" "\\1" Sodium_VERSION_MINOR ${minor-line})
	set(Sodium_LIBRARY_VERSION ${Sodium_VERSION_MAJOR}.${Sodium_VERSION_MINOR})
	unset(version-strings)
	unset(major-line)
	unset(minor-line)
endif()

if(NOT SODIUM_LIBRARY)
	find_library(Sodium_LIBRARY
		NAMES
			sodium
		PATHS
			${SODIUM_DIR}
		DOC "Sodium is an easy-to-use software library that provides a wide range of cryptographic operations"
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(Sodium_LIBRARY_DEBUG
			NAMES
				sodiumd
			PATHS
				${SODIUM_DIR}
		)
		mark_as_advanced(Sodium_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sodium
	REQUIRED_VARS
		Sodium_LIBRARY
		SODIUM_INCLUDE_DIR
	VERSION_VAR
		Sodium_VERSION
)

if(Sodium_FOUND AND NOT TARGET Sodium::Sodium)
	add_library(Sodium::Sodium UNKNOWN IMPORTED)
	set_target_properties(Sodium::Sodium
		PROPERTIES
			IMPORTED_LOCATION ${Sodium_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${SODIUM_INCLUDE_DIR}
	)
	set_property(TARGET Sodium::Sodium APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	set_target_properties(Sodium::Sodium
		PROPERTIES
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
			IMPORTED_LOCATION_RELEASE "${Sodium_LIBRARY}"
	)
	if(EXISTS "${Sodium_LIBRARY_DEBUG}")
		set_property(TARGET Sodium::Sodium APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(Sodium::Sodium
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
				IMPORTED_LOCATION_DEBUG "${Sodium_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
