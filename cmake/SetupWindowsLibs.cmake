# Checks out the SVN Revision of windows libraries. Additionally sets up the environment for cmake.
# To update the libraries change the 'SVN_REVISION'. Important: the '-r' in the Revision must be retained.
message(STATUS "Fetching Windows prebuild Libraries for ${ARCH}")
if(fetch-prebuild-libs)
	include(FetchContent)
	if(ARCH STREQUAL "amd64")
		set(REPO_NAME windows-libs-amd64)
	else()
		set(REPO_NAME windows-libs)
	endif()

	FetchContent_Populate(
		prebuild_libs
		SVN_REPOSITORY	https://svn.wildfiregames.com/public/${REPO_NAME}/trunk
		SVN_REVISION	-r28278
		SOURCE_DIR		${0AD_EXT_LIBDIR}
	)
endif()
message(STATUS "Copy dependencies' binaries to 'binaries/system/' and adding to 'CMAKE_PREFIX_PATH'")
set(DIR_LIST cpp-httplib enet fcollada freetype gloox iconv icu libcurl libpng libsodium libxml2 microsoft miniupnpc nvtt openal sdl2 spidermonkey vorbis zlib)
foreach(dir ${DIR_LIST})
	file(COPY ${0AD_EXT_LIBDIR}/${dir}/bin DESTINATION ${CMAKE_SOURCE_DIR}/binaries/system)
	list(APPEND CMAKE_PREFIX_PATH ${0AD_EXT_LIBDIR}/${dir})
endforeach()
# Add the whole libraries directory to CMAKE_PREFIX_PATH. May be redundand but needed for wxWidgets
list(APPEND CMAKE_PREFIX_PATH ${0AD_EXT_LIBDIR})
# Add libraries not set during binary copy
list(APPEND CMAKE_PREFIX_PATH ${0AD_EXT_LIBDIR}/fmt)
list(APPEND CMAKE_PREFIX_PATH ${0AD_EXT_LIBDIR}/libzip)
list(APPEND CMAKE_PREFIX_PATH ${0AD_EXT_LIBDIR}/cxxtest-4.4)

message(STATUS "Copy build tools to 'build/bin'")
file(COPY ${0AD_EXT_LIBDIR}/cxxtest-4.4/bin DESTINATION ${CMAKE_SOURCE_DIR}/build/bin)
