# Locate fmt
# This module provides the following Imported Targets:
#	fmt::fmt
# These variables may optionally be set to help this module find the correct files:
#	FMT_LIBRARY
#	FMT_INCLUDE_DIR

if(NOT FMT_INCLUDE_DIR)
	find_path(FMT_INCLUDE_DIR fmt/base.h
		PATHS
			${FMT_DIR}
	)

	# Parse version number from include Header
	file(STRINGS "${FMT_INCLUDE_DIR}/fmt/base.h" version-strings
		REGEX "#define[ \t]+FMT_VERSION.*"
	)
	list(GET version-strings 0 version-line)
	# CMake does not support {n}
	string(REGEX MATCHALL "([0-9][0-9])" fmt_version_number ${version-line})
	list(GET fmt_version_number 0 fmt_VERSION_MAJOR)
	list(GET fmt_version_number 1 fmt_VERSION_MINOR)
	list(GET fmt_version_number 2 fmt_VERSION_PATCH)
	set(fmt_VERSION ${fmt_VERSION_MAJOR}.${fmt_VERSION_MINOR}.${fmt_VERSION_PATCH})
	unset(version-strings)
	unset(version-line)
	unset(fmt_version_number)
endif()

if(NOT FMT_LIBRARY)
	find_library(FMT_LIBRARY
		NAMES
			fmt
		PATHS
			${FMT_DIR}
		DOC "{fmt} is an open-source formatting library providing a fast and safe alternative to C stdio and C++ iostreams."
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(FMT_LIBRARY_DEBUG
			NAMES
				fmtd
			PATHS
				${FMT_DIR}
		)
		mark_as_advanced(FMT_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(fmt
	REQUIRED_VARS
		FMT_LIBRARY
		FMT_INCLUDE_DIR
	VERSION_VAR
		fmt_VERSION
)

if(fmt_FOUND AND NOT TARGET fmt::fmt)
	add_library(fmt::fmt UNKNOWN IMPORTED)
	set_target_properties(fmt::fmt
		PROPERTIES
			IMPORTED_LOCATION ${FMT_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${FMT_INCLUDE_DIR}
	)
	set_property(TARGET fmt::fmt APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	set_target_properties(fmt::fmt
		PROPERTIES
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${FMT_LIBRARY}"
	)
	if(EXISTS "${FMT_LIBRARY_DEBUG}")
		set_property(TARGET fmt::fmt APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(fmt::fmt
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${FMT_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
