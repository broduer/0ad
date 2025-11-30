# Find FCollada bundled version
# This module provides the following Imported Targets:
#	FCollada::FCollada
# These variables may optionally be set to help this module find the correct files:
#	FCOLLADA_INCLUDE_DIR
#	FCOLLADA_DIR

if(UNIX)
	set(FCOLLADA_SEARCH_PATH ${CMAKE_SOURCE_DIR}/libraries/source/fcollada)
else()
	set(FCOLLADA_SEARCH_PATH ${0AD_EXT_LIBDIR}/fcollada)
endif()
find_path(FCOLLADA_INCLUDE_DIR FCollada.h
	PATHS
		${FCOLLADA_DIR}
		${FCOLLADA_SEARCH_PATH}
	PATH_SUFFIXES
		include
	NO_DEFAULT_PATH
)

# Parse version number from include Header
file(STRINGS "${FCOLLADA_INCLUDE_DIR}/FCollada.h" version-strings
	REGEX "#define[ \t]+FCOLLADA_VERSION.*"
)
list(GET version-strings 0 version-line)
# CMake does not support {n}
string(REGEX MATCHALL "([0-9][0-9][0-9][0-9])" fcollada_version_number ${version-line})
list(GET fcollada_version_number 0 FCollada_VERSION_MAJOR)
list(GET fcollada_version_number 1 FCollada_VERSION_MINOR)
string(REGEX REPLACE "[0]+" "" FCollada_VERSION_MAJOR ${FCollada_VERSION_MAJOR})
string(REGEX REPLACE "[0]+" "" FCollada_VERSION_MINOR ${FCollada_VERSION_MINOR})
set(FCollada_VERSION ${FCollada_VERSION_MAJOR}.${FCollada_VERSION_MINOR})
unset(version-strings)
unset(version-line)
unset(fcollada_version_number)

if(UNIX)
	find_library(FCOLLADA_LIBRARY
		NAMES
			FColladaSR
		PATHS
			${FCOLLADA_DIR}
			${FCOLLADA_SEARCH_PATH}
		PATH_SUFFIXES
			lib/
		DOC "FCollada Library(Bundled)"
		NO_DEFAULT_PATH
	)
	find_library(FCOLLADA_LIBRARY_DEBUG
		NAMES
			FColladaSR
		PATHS
			${FCOLLADA_DIR}
			${FCOLLADA_SEARCH_PATH}
		PATH_SUFFIXES
			lib/
		NO_DEFAULT_PATH
	)
else()
	find_library(FCOLLADA_LIBRARY
		NAMES
			FCollada
		PATHS
			${FCOLLADA_DIR}
			${FCOLLADA_SEARCH_PATH}
		PATH_SUFFIXES
			lib/
		DOC "FCollada Library(Bundled)"
		NO_DEFAULT_PATH
	)
	find_library(FCOLLADA_LIBRARY_DEBUG
		NAMES
			FColladaD
		PATHS
			${FCOLLADA_DIR}
			${FCOLLADA_SEARCH_PATH}
		PATH_SUFFIXES
			lib/
		NO_DEFAULT_PATH
	)
endif()
mark_as_advanced(FCOLLADA_LIBRARY_DEBUG FCOLLADA_LIBRARY_SR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FCollada
	REQUIRED_VARS
		FCOLLADA_LIBRARY
		FCOLLADA_INCLUDE_DIR
	VERSION_VAR
		FCollada_VERSION
)

if (FCollada_FOUND AND NOT TARGET FCollada::FCollada)
	add_library(FCollada::FCollada UNKNOWN IMPORTED)
	target_link_libraries(FCollada::FCollada
		INTERFACE
			${FCOLLADA_LIBRARY_SR}
	)
	set_target_properties(FCollada::FCollada
		PROPERTIES
			IMPORTED_LOCATION ${FCOLLADA_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${FCOLLADA_INCLUDE_DIR}
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${FCOLLADA_LIBRARY}"
	)
	set_property(TARGET FCollada::FCollada APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	if(EXISTS "${FCOLLADA_LIBRARY_DEBUG}")
		set_property(TARGET FCollada::FCollada APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(FCollada::FCollada
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${FCOLLADA_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
