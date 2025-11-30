# Locate ENet
# This module provides the following Imported Targets:
#	ENet::ENet
# These variables may optionally be set to help this module find the correct files:
#	ENET_LIBRARY
#	ENET_INCLUDE_DIR

if(NOT ENET_INCLUDE_DIR)
	find_path(ENET_INCLUDE_DIR enet/enet.h
		PATHS
			${ENET_DIR}
	)

	# Parse version number from include Header
	file(STRINGS "${ENET_INCLUDE_DIR}/enet/enet.h" version-strings
		REGEX "#define[ \t]+ENET_VERSION_(MAJOR|MINOR|PATCH).*"
	)
	list(GET version-strings 0 major-line)
	list(GET version-strings 1 minor-line)
	list(GET version-strings 2 patch-line)
	string(REGEX REPLACE "^#define[ \t]+ENET_VERSION_MAJOR[ \t]+([0-9]+)" "\\1" ENet_VERSION_MAJOR ${major-line})
	string(REGEX REPLACE "^#define[ \t]+ENET_VERSION_MINOR[ \t]+([0-9]+)" "\\1" ENet_VERSION_MINOR ${minor-line})
	string(REGEX REPLACE "^#define[ \t]+ENET_VERSION_PATCH[ \t]+([0-9]+)" "\\1" ENet_VERSION_PATCH ${patch-line})
	set(ENet_VERSION ${ENet_VERSION_MAJOR}.${ENet_VERSION_MINOR}.${ENet_VERSION_PATCH})
	unset(version-strings)
	unset(major-line)
	unset(minor-line)
	unset(patch-line)
endif()

if(NOT ENET_LIBRARY)
	find_library(ENET_LIBRARY
		NAMES
			enet
		PATHS
			${ENET_DIR}
		DOC "ENet - Simple, lightweight and reliable UDP networking library written on pure C"
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(ENET_LIBRARY_DEBUG
			NAMES
				enetd
			PATHS
				${ENET_DIR}
		)
		mark_as_advanced(ENET_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ENet
	REQUIRED_VARS
		ENET_LIBRARY
		ENET_INCLUDE_DIR
	VERSION_VAR
		ENet_VERSION
)

if(ENet_FOUND AND NOT TARGET ENet::ENet)
	add_library(ENet::ENet UNKNOWN IMPORTED)
	set_target_properties(ENet::ENet
		PROPERTIES
			IMPORTED_LOCATION ${ENET_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${ENET_INCLUDE_DIR}
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
			IMPORTED_LOCATION_RELEASE "${ENET_LIBRARY}"
	)
	set_property(TARGET ENet::ENet APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	if(EXISTS "${ENET_LIBRARY_DEBUG}")
		set_property(TARGET ENet::ENet APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(ENet::ENet
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
				IMPORTED_LOCATION_DEBUG "${ENET_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
