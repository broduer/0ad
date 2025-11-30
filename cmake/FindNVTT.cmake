# Locate nvidia-texture-tools (Code based on https://github.com/openscenegraph/OpenSceneGraph/blob/master/CMakeModules/FindNVTT.cmake)
# This module provides the following Imported Targets:
#	nvtt::nvtt
# These variables may optionally be set to help this module find the correct files:
#	NVTT_LIBRARY
#	NVTT_INCLUDE_DIR

find_path(NVTT_INCLUDE_DIR nvtt/nvtt.h
	PATHS
		${NVTT_DIR}
)

# NVTT
find_library(NVTT_LIBRARY
	NAMES
		nvtt
	PATHS
		${NVTT_DIR}
)

find_library(NVTT_LIBRARY_DEBUG
	NAMES
		nvttd
	PATHS
		${NVTT_DIR}
)

# NVIMAGE
find_library(NVIMAGE_LIBRARY
	NAMES nvimage
	PATHS
		${NVTT_DIR}
)

# NVMATH
find_library(NVMATH_LIBRARY
	NAMES nvmath
	PATHS
		${NVTT_DIR}
)

find_library(NVMATH_LIBRARY_DEBUG
	NAMES
		nvmathd
	PATHS
		${NVTT_DIR}
)

# NVCORE
find_library(NVCORE_LIBRARY
	NAMES
		nvcore
	PATHS
		${NVTT_DIR}
)
find_library(NVCORE_LIBRARY_DEBUG
	NAMES
		nvcored
	PATHS
		${NVTT_DIR}
)

# NVTHREAD
find_library(NVTHREAD_LIBRARY
	NAMES
		nvthread
	PATHS
		${NVTT_DIR}
)
find_library(NVTHREAD_LIBRARY_DEBUG
	NAMES
		nvthreadd
	PATHS
		${NVTT_DIR}
)

# SQUISH
find_library(NVSQUISH_LIBRARY
	NAMES
		squish
	PATHS
		${NVTT_DIR}
)
find_library(NVSQUISH_LIBRARY_DEBUG
	NAMES
		squishd
	PATHS
		${NVTT_DIR}
)

# BC6H
find_library(NVBC6H_LIBRARY
	NAMES
		bc6h
	PATHS
		${NVTT_DIR}
)
find_library(NVBC6H_LIBRARY_DEBUG
	NAMES
		bc6hd
	PATHS
		${NVTT_DIR}
)

# BC7
find_library(NVBC7_LIBRARY
	NAMES
		bc7
	PATHS
		${NVTT_DIR}
)
find_library(NVBC7_LIBRARY_DEBUG
	NAMES
		bc7d
	PATHS
		${NVTT_DIR}
)

if(WIN32)
	set(LIBS_TO_setUP "NVTT" "NVCORE" "NVMATH" "NVIMAGE" "NVTHREAD" "NVBC7" "NVBC6H" "NVSQUISH")
else()
	set(LIBS_TO_setUP "NVTT" "NVCORE" "NVMATH" "NVIMAGE")
endif()


foreach(LIB ${LIBS_TO_setUP})
	if(${LIB}_LIBRARY_DEBUG)
		set(${LIB}_LIBRARIES optimized ${${LIB}_LIBRARY} debug ${${LIB}_LIBRARY_DEBUG})
	else(${LIB}_LIBRARY_DEBUG)
		set(${LIB}_LIBRARY_DEBUG ${${LIB}_LIBRARY})
		set(${LIB}_LIBRARIES optimized ${${LIB}_LIBRARY} debug ${${LIB}_LIBRARY_DEBUG})
	endif(${LIB}_LIBRARY_DEBUG)
endforeach(LIB ${LIBS_TO_setUP})

set(NVTT_LIBRARIES
	${NVTT_LIBRARIES}
	${NVCORE_LIBRARIES}
	${NVMATH_LIBRARIES}
	${NVIMAGE_LIBRARIES}
	${NVTHREAD_LIBRARIES}
	${NVBC7_LIBRARIES}
	${NVBC6H_LIBRARIES}
	${NVSQUISH_LIBRARIES}
)

mark_as_advanced(NVTT_LIBRARY_DEBUG NVTT_NVCORE)

find_package_handle_standard_args(NVTT
	REQUIRED_VARS
		NVTT_LIBRARY
		NVTT_INCLUDE_DIR
	VERSION_VAR
		NVTT_VERSION
)

if (NVTT_FOUND AND NOT TARGET nvtt::nvtt)
	add_library(nvtt::nvtt UNKNOWN IMPORTED)
	target_link_libraries(nvtt::nvtt
		INTERFACE
			${NVTT_LIBRARIES}
	)
	set_target_properties(nvtt::nvtt
		PROPERTIES
			IMPORTED_LOCATION ${NVTT_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${NVTT_INCLUDE_DIR}
			IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
			IMPORTED_LOCATION_RELEASE "${NVTT_LIBRARY}"
	)
	set_property(TARGET nvtt::nvtt APPEND PROPERTY
		IMPORTED_CONFIGURATIONS RELEASE
	)
	if(EXISTS "${NVTT_LIBRARY_DEBUG}")
		set_property(TARGET nvtt::nvtt APPEND PROPERTY
			IMPORTED_CONFIGURATIONS DEBUG
		)
		set_target_properties(nvtt::nvtt
			PROPERTIES
				IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
				IMPORTED_LOCATION_DEBUG "${NVTT_LIBRARY_DEBUG}"
				MAP_IMPORTED_CONFIG_MINSIZEREL Release
				MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
		)
	endif()
endif()
