# Locate Vorbis
# This module encapsulates ogg and vorbisfile.
# This module provides the following Imported Targets:
#	Vorbis::Vorbis
# These variables may optionally be set to help this module find the correct files:
#	VORBIS_LIBRARY
#	VORBIS_INCLUDE_DIR

if(NOT VORBIS_INCLUDE_DIR)
	find_path(VORBIS_INCLUDE_DIR vorbis/vorbisfile.h
		PATHS
			${VORBIS_DIR}
	)
	find_path(OGG_INCLUDE_DIR ogg/ogg.h
		PATHS
			${VORBIS_DIR}
	)
endif()

if(NOT VORBIS_LIBRARY)
	find_library(VORBIS_LIBRARY
		NAMES
			vorbis
		PATHS
			${VORBIS_DIR}
		DOC ""
	)
	find_library(VORBISFILE_LIBRARY
		NAMES
			vorbisfile
		PATHS
			${VORBIS_DIR}
		DOC ""
	)
	find_library(OGG_LIBRARY
		NAMES
			ogg
		PATHS
			${VORBIS_DIR}
		DOC ""
	)
	if(CMAKE_SYSTEM_NAME MATCHES "Windows")
		find_library(VORBIS_LIBRARY_DEBUG
			NAMES
				vorbisd
			PATHS
				${VORBIS_DIR}
		)
		find_library(VORBISFILE_LIBRARY_DEBUG
			NAMES
				vorbisfiled
			PATHS
				${VORBIS_DIR}
			DOC ""
		)
		find_library(OGG_LIBRARY_DEBUG
			NAMES
				oggd
			PATHS
				${VORBIS_DIR}
			DOC ""
		)
		mark_as_advanced(VORBIS_LIBRARY_DEBUG VORBISFILE_LIBRARY_DEBUG OGG_LIBRARY_DEBUG)
	endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Vorbis
	REQUIRED_VARS
		VORBIS_LIBRARY
		VORBISFILE_LIBRARY
		OGG_LIBRARY
		VORBIS_INCLUDE_DIR
		OGG_INCLUDE_DIR
	VERSION_VAR
		Vorbis_VERSION
)

if(Vorbis_FOUND AND NOT TARGET Vorbis::Vorbis)
	add_library(Vorbis::Vorbis UNKNOWN IMPORTED)
	target_link_libraries(Vorbis::Vorbis
		INTERFACE
			${VORBISFILE_LIBRARY}
			${OGG_LIBRARY}
			$<$<CONFIG:Debug>:${OGG_LIBRARY_DEBUG}>
			$<$<CONFIG:Debug>:${VORBISFILE_LIBRARY_DEBUG}>
	)
	set_target_properties(Vorbis::Vorbis
		PROPERTIES
			IMPORTED_LOCATION ${VORBIS_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${VORBIS_INCLUDE_DIR}
	)
	set_property(TARGET Vorbis::Vorbis APPEND PROPERTY
		INTERFACE_INCLUDE_DIRECTORIES ${OGG_INCLUDE_DIR}
	)
	set_property(TARGET Vorbis::Vorbis APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	set_target_properties(Vorbis::Vorbis
		PROPERTIES
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${VORBIS_LIBRARY}"
	)
	if(EXISTS "${VORBIS_LIBRARY_DEBUG}")
		set_property(TARGET Vorbis::Vorbis APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(Vorbis::Vorbis
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
				IMPORTED_LOCATION_DEBUG "${VORBIS_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
