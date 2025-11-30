# Locate Valgrind
# This module provides the following Imported Targets:
#	Valgrind::Valgrind
# This module requires pkg-config, otherwise it will return an error.


find_package(PkgConfig QUIET REQUIRED)
pkg_check_modules(VALGRIND_PKGCFG valgrind QUIET IMPORTED_TARGET)
set(Valgrind_VERSION ${VALGRIND_PKGCFG_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Valgrind
	REQUIRED_VARS
		VALGRIND_PKGCFG_LIBRARIES
		VALGRIND_PKGCFG_INCLUDE_DIRS
	VERSION_VAR
		Valgrind_VERSION
)

if(Valgrind_FOUND AND NOT TARGET Valgrind::Valgrind)
	add_library(Valgrind::Valgrind INTERFACE IMPORTED)
	# add_library(Valgrind::Valgrind UNKNOWN IMPORTED)
	set_target_properties(Valgrind::Valgrind
		PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES ${VALGRIND_PKGCFG_INCLUDE_DIRS}
			# IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
	)
	# set_property(TARGET Valgrind::Valgrind APPEND PROPERTY
	# 	IMPORTED_LOCATION ${VALGRIND_PKGCFG_LINK_LIBRARIES}
	# )
	# set_property(TARGET Valgrind::Valgrind APPEND PROPERTY
	# 	IMPORTED_LOCATION_RELEASE ${VALGRIND_PKGCFG_LINK_LIBRARIES}
	# )
	# set_property(TARGET Valgrind::Valgrind APPEND PROPERTY
	# 	IMPORTED_CONFIGURATIONS RELEASE
	# )
endif()
