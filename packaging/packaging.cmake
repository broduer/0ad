# Create Package for Linux (RPM oder DEB ) und Windows (NSIS)

# ++++++++++++++++++ General ++++++++++++++++++++++++++++++++++++++++++++++++++
set(PROJECT_OWNER "Wildfiregames")
set(PROJECT_OWNER_EMAIL "")
set(SECTION_GROUP "Games")

set(CPACK_PACKAGE_VENDOR ${PROJECT_OWNER})

set(CPACK_VERBATIM_VARIABLES YES)

set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/_packages")

# ++++++++++++++++++ License and README +++++++++++++++++++++++++++++++++++++++
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE.md)
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/README.md)

# ++++++++++++++++++ Debian specific ++++++++++++++++++++++++++++++++++++++++++
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PROJECT_OWNER} <${PROJECT_OWNER_EMAIL}>")
set(CPACK_DEBIAN_PACKAGE_SECTION ${SECTION_GROUP})
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "0ad-data (>= ${CMAKE_PROJECT_VERSION})")
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)

# ++++++++++++++++++ RPM specific +++++++++++++++++++++++++++++++++++++++++++++
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_GROUP ${SECTION_GROUP})
set(CPACK_RPM_PACKAGE_REQUIRES "0ad-data >= ${CMAKE_PROJECT_VERSION}")

# ++++++++++++++++++ Windows specific +++++++++++++++++++++++++++++++++++++++++


# ++++++++++++++++++ Generator selection ++++++++++++++++++++++++++++++++++++++
if (CMAKE_SYSTEM MATCHES "Linux")
	set(BACKENDS)
	find_program(DEB_AVAILABLE NAMES dpkg OPTIONAL)
	find_program(RPM_AVAILABLE NAMES rpmbuild)

	if(DEB_AVAILABLE)
		list(APPEND BACKENDS DEB)
		message(STATUS "DEB backend available")
	elseif(RPM_AVAILABLE)
		list(APPEND BACKENDS RPM)
		message(STATUS "RPM backend available")
	endif()

	if(DEB_AVAILABLE)
		message(STATUS "Using DEB backend")
	elseif(RPM_AVAILABLE)
		message(STATUS "Using RPM backend")
	endif()

	set(CPACK_GENERATOR TGZ ${BACKENDS})
elseif (CMAKE_SYSTEM MATCHES "Darwin")
	set(CPACK_GENERATOR TGZ DragNDrop)
elseif (CMAKE_SYSTEM MATCHES "Windows")
	set(CPACK_GENERATOR NSIS)
else()
	set(CPACK_GENERATOR TGZ)
endif()

include(CPack)
