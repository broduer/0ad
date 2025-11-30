# Locate miniupnpc
# This module provides the following Imported Targets:
#	Miniupnpc::Miniupnpc
# These variables may optionally be set to help this module find the correct files:
#	MINIUPNPC_LIBRARY
#	MINIUPNPC_INCLUDE_DIR

if(NOT MINIUPNPC_INCLUDE_DIR)
	find_path(MINIUPNPC_INCLUDE_DIR miniupnpc/miniupnpc.h
		PATHS
			${MINIUPNPC_DIR}
	)

	# Parse version number from include Header
	file(STRINGS "${MINIUPNPC_INCLUDE_DIR}/miniupnpc/miniupnpc.h" version-strings
		REGEX "#define[ \t]+MINIUPNPC_VERSION.*"
	)
	list(GET version-strings 0 version-line)
	string(REGEX MATCH "([0-9]+\\.)?([0-9]+\\.)?(\\*|[0-9]+)" Miniupnpc_VERSION ${version-line})
	unset(version-strings)
	unset(version-line)
endif()

if(NOT MINIUPNPC_LIBRARY)
	find_library(MINIUPNPC_LIBRARY
		NAMES
			miniupnpc
		PATHS
			${MINIUPNPC_DIR}
		DOC ""
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(MINIUPNPC_LIBRARY_DEBUG
			NAMES
				miniupnpcd
			PATHS
				${MINIUPNPC_DIR}
		)
		mark_as_advanced(MINIUPNPC_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Miniupnpc
	REQUIRED_VARS
		MINIUPNPC_LIBRARY
		MINIUPNPC_INCLUDE_DIR
	VERSION_VAR
		Miniupnpc_VERSION
)

if(Miniupnpc_FOUND AND NOT TARGET Miniupnpc::Miniupnpc)
	add_library(Miniupnpc::Miniupnpc UNKNOWN IMPORTED)
	set_target_properties(Miniupnpc::Miniupnpc
		PROPERTIES
			IMPORTED_LOCATION ${MINIUPNPC_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${MINIUPNPC_INCLUDE_DIR}
	)
	set_property(TARGET Miniupnpc::Miniupnpc APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	set_target_properties(Miniupnpc::Miniupnpc
		PROPERTIES
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
			IMPORTED_LOCATION_RELEASE "${MINIUPNPC_LIBRARY}"
	)
	if(EXISTS "${MINIUPNPC_LIBRARY_DEBUG}")
		set_property(TARGET Miniupnpc::Miniupnpc APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(Miniupnpc::Miniupnpc
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
				IMPORTED_LOCATION_DEBUG "${MINIUPNPC_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
