# Locate CppHTTPlib
# This module provides the following Imported Targets:
#	CppHTTPlib::CppHTTPlib
# These variables may optionally be set to help this module find the correct files:
#	CPPHTTPLIB_LIBRARY
#	CPPHTTPLIB_INCLUDE_DIR

if(NOT CPPHTTPLIB_INCLUDE_DIR)
	find_path(CPPHTTPLIB_INCLUDE_DIR httplib.h
		PATHS
			${CPPHTTPLIB_DIR}
	)

	# Parse version number from include Header
	file(STRINGS "${CPPHTTPLIB_INCLUDE_DIR}/httplib.h" version-strings
		REGEX "#define[ \t]+CPPHTTPLIB_VERSION.*"
	)
	list(GET version-strings 0 version-line)
	string(REGEX MATCH "([0-9]+\\.)?([0-9]+\\.)?(\\*|[0-9]+)" CppHTTPlib_VERSION ${version-line})
	unset(version-strings)
	unset(version-line)
endif()

if(NOT CPPHTTPLIB_LIBRARY)
	find_library(CPPHTTPLIB_LIBRARY
		NAMES
			cpp-httplib
		PATHS
			${CPPHTTPLIB_DIR}
		DOC "A C++11 single-file header-only cross platform HTTP/HTTPS library."
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(CPPHTTPLIB_LIBRARY_DEBUG
			NAMES
				cpp-httplibd
			PATHS
				${CPPHTTPLIB_DIR}
		)
		mark_as_advanced(CPPHTTPLIB_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CppHTTPlib
	REQUIRED_VARS
		CPPHTTPLIB_LIBRARY
		CPPHTTPLIB_INCLUDE_DIR
	VERSION_VAR
		CppHTTPlib_VERSION
)

if(CppHTTPlib_FOUND AND NOT TARGET CppHTTPlib::CppHTTPlib)
	add_library(CppHTTPlib::CppHTTPlib UNKNOWN IMPORTED)
	set_target_properties(CppHTTPlib::CppHTTPlib
		PROPERTIES
			IMPORTED_LOCATION ${CPPHTTPLIB_LIBRARY}
			IMPORTED_LOCATION_RELEASE ${CPPHTTPLIB_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${CPPHTTPLIB_INCLUDE_DIR}
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
	)
	set_property(TARGET CppHTTPlib::CppHTTPlib APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	if(EXISTS "${CPPHTTPLIB_LIBRARY_DEBUG}")
		set_property(TARGET CppHTTPlib::CppHTTPlib APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(CppHTTPlib::CppHTTPlib
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${CPPHTTPLIB_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
