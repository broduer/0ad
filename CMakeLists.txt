cmake_minimum_required(VERSION 3.25.1...4.0.0)

project(0ad VERSION 0.29.0)

# Available Options
option(android "Use non-working Android cross-compiling mode")
option(build-docs "Enable building the doxygen documentation(requires Network access)")
option(coverage "Enable code coverage data collection (GCC only)")
option(gles "Use non-working OpenGL ES 2.0 mode")
option(jenkins-tests "Configure CxxTest to use the XmlPrinter runner which produces Jenkins-compatible output")
option(minimal-flags "Only set compiler/linker flags that are really needed. Has no effect on Windows builds")
option(sanitize-address "Enable ASAN if available")
option(sanitize-thread "Enable TSAN if available")
option(sanitize-undefined-behaviour "Enable UBSAN if available")
option(with-system-cpp-httplib "Search standard paths for cpp-httplib, instead of using bundled copy")
option(with-system-cxxtest "Search standard paths for cxxtest, instead of using bundled copy")
option(with-lto "Enable Link Time Optimization (LTO)")
option(with-system-mozjs "Search standard paths for libmozjs115, instead of using bundled copy")
option(with-system-nvtt "Search standard paths for nvidia-texture-tools library, instead of using bundled copy")
option(with-valgrind "Enable Valgrind support (non-Windows only)")
option(without-audio "Disable use of OpenAL/Ogg/Vorbis APIs")
option(without-atlas "Disable Atlas scenario/map editor and ActorEditor")
option(without-dap-interface "Disable Dap interface project")
option(without-lobby "Disable the use of gloox and the multiplayer lobby")
option(without-miniupnpc "Disable use of miniupnpc for port forwarding")
option(without-nvtt "Disable use of NVTT")
option(without-pch "Disable generation and usage of precompiled headers")
option(without-tests "Disable generation of test projects")

# Windows specific options
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	option(fetch-prebuild-libs "Fetch prebuild libraries from SVN. Defaults to ON." ON)
endif()

# OS X specific options
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	option(macosx-version-min "Set minimum required version of the OS X API, the build will possibly fail if an older SDK is used, while newer API functions will be weakly linked (i.e. resolved at runtime)")
	option(sysroot "Set compiler system root path, used for building against a non-system SDK. For example /usr/local becomes SYSROOT/user/local")
endif()

# Set the default build type if not specified (Only for single configuration generators)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Install options
set(bindir "" CACHE STRING "Directory for executables (typically '/usr/games'); default is to be relocatable")
set(datadir "" CACHE STRING "Directory for data files (typically '/usr/share/games/0ad'); default is ../data/ relative to executable")
set(libdir "" CACHE STRING "Directory for libraries (typically '/usr/lib/games/0ad'); default is ./ relative to executable")

# +++++++++++++++++++++ General Setup ++++++++++++++++++++

# Default Cache variables
# Append to the Modulepath. Allows to make custom cmake modules.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
# Set default Arch
set(ARCH "x86" CACHE STRING "CPU Architecture. Possible values are arm, aarch64, x86, amd64, e2k, ppc64, loong64, riscv64")
set(MACOS_ARCH "x86_64" CACHE STRING "Mac OS specific Architecture. Possible values are arm64 and x86_64")
option(link_execinfo "")
option(mozjs_is_debug_build "")

# Change CMakes default OUTPUT directories, as paths are loaded relative from root/binaries/system (Setting them per output is too tedious)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries/system)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries/system)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries/system)

# Detect CPU architecture (simplistic). The user can target an architecture by setting '-DARCH=arch', but the game still selects some know value.
if(android)
	set(ARCH "arm" CACHE STRING "CPU Architecture. Possible values are arm, aarch64, x86, amd64, e2k, ppc64, loong64, riscv64" FORCE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	if(NOT CMAKE_VS_PLATFORM_NAME)
		if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "AMD64" OR ENV{PROCESSOR_ARCHITEW6432} STREQUAL "AMD64")
			set(ARCH "amd64" CACHE STRING "CPU Architecture. Possible values are arm, aarch64, x86, amd64, e2k, ppc64, loong64, riscv64" FORCE)
		endif()
	else()
		if(CMAKE_VS_PLATFORM_NAME MATCHES "x64")
			set(ARCH "amd64" CACHE STRING "CPU Architecture. Possible values are arm, aarch64, x86, amd64, e2k, ppc64, loong64, riscv64" FORCE)
		endif()
	endif()
else()
	# could be parsed from the same command as premakes version
	if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(x86_64|x64)")
			set(MACOS_ARCH "x86_64" CACHE STRING "Mac OS specific Architecture. Possible values are arm64 and x86_64" FORCE)
		elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
			set(MACOS_ARCH "arm64" CACHE STRING "Mac OS specific Architecture. Possible values are arm64 and x86_64" FORCE)
		endif()
	elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(x86_64|x64)")
		set(ARCH "amd64" CACHE STRING "CPU Architecture. Possible values are arm, aarch64, x86, amd64, e2k, ppc64, loong64, riscv64" FORCE)
	elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "(aarch64|aarch64_be)")
		set(ARCH "aarch64" CACHE STRING "CPU Architecture. Possible values are arm, aarch64, x86, amd64, e2k, ppc64, loong64, riscv64" FORCE)
	else()
		message(WARNING "Cannot determine architecture from GCC, assuming x86")
	endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	if(ARCH MATCHES "amd64")
		set(0AD_EXT_LIBDIR ${CMAKE_SOURCE_DIR}/libraries/win64 CACHE STRING "Extern libraries directory of 0ad. Can be referenced in other CMakeLists.txt files.")
	else()
		set(0AD_EXT_LIBDIR ${CMAKE_SOURCE_DIR}/libraries/win32 CACHE STRING "Extern libraries directory of 0ad. Can be referenced in other CMakeLists.txt files.")
	endif()
endif()

# On Windows check if wxWidgets is available, if not disable atlas and emit warning. This is because there are currently no prebuilt binaries provided.
if(NOT without-atlas AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
	if(NOT EXISTS "${0AD_EXT_LIBDIR}/wxwidgets/include/wx/wx.h")
		message(STATUS "wxWidgets not found, disabling atlas")
		set(without-atlas ON CACHE BOOL "Disable Atlas scenario/map editor and ActorEditor" FORCE)
	endif()
endif()

# Test whether we need to link libexecinfo. This is mostly the case on musl systems, as well as on BSD systems : only glibc provides the
# backtrace symbols we require in the libc, for other libcs we use the libexecinfo library.
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	set(link_execinfo ON CACHE BOOL "" FORCE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	include(CheckCSourceCompiles)
	file(READ ${CMAKE_SOURCE_DIR}/build/premake/tests/execinfo.c content)
	check_c_source_compiles([[${content}]] LINK_EXECINFO)
	if(NOT LINK_EXECINFO)
		set(link_execinfo ON CACHE BOOL "" FORCE)
	else()
		set(link_execinfo OFF CACHE BOOL "" FORCE)
	endif()
	unset(LINK_EXECINFO)
	unset(content)
endif()

# Test whether system mozjs is built with --enable-debug. The pc file doesn't specify the required -DDEBUG needed in that case
# Currently only working on bash shell!
if(with-system-mozjs)
	execute_process(
		COMMAND bash "-c" "${CMAKE_C_COMPILER} $(pkg-config mozjs-128 --cflags) ${CMAKE_SOURCE_DIR}/build/premake/tests/mozdebug.c -o /dev/null"
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		RESULT_VARIABLE errorCode
		OUTPUT_QUIET
		ERROR_QUIET
	)
	if(NOT errorCode EQUAL 0)
		set(mozjs_is_debug_build ON CACHE BOOL "" FORCE)
	else()
		set(mozjs_is_debug_build OFF CACHE BOOL "" FORCE)
	endif()
endif()

if(NOT without-tests)
	enable_testing()
endif()

# Include the BuildFlags target only once after all setup is finished
include(0ad-BuildFlags)
include(packaging/packaging.cmake)

# +++++++++++++++++++++ Include library setups ++++++++++++++++++++
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	include(SetupWindowsLibs)
elseif(UNIX)
	include(SetupUnixLibs)
	if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
		include(SetupMacOSLibs)
	endif()
endif()

# Add Pyrogenesis main project
add_subdirectory(${CMAKE_SOURCE_DIR}/source)

# Add Collada project
add_subdirectory(${CMAKE_SOURCE_DIR}/source/collada)

# Add subprojects
if(NOT without-atlas)
	add_subdirectory(${CMAKE_SOURCE_DIR}/source/tools/atlas/)
endif()

# Add doxygen target
if(build-docs)
	add_subdirectory(${CMAKE_SOURCE_DIR}/docs/doxygen)
endif()

# +++++++++++++++++++ Install section +++++++++++++++++++++++++++++++++++++++++
include(GNUInstallDirs)
# 0ad Resources
install(
	FILES
		${CMAKE_SOURCE_DIR}/build/resources/0ad.desktop
	DESTINATION
		${CMAKE_INSTALL_DATADIR}/applications
	COMPONENT	resources
)

install(
	FILES
		${CMAKE_SOURCE_DIR}/build/resources/0ad.appdata.xml
	DESTINATION
		${CMAKE_INSTALL_DATADIR}/metainfo
	COMPONENT	resources
)

install(
	PROGRAMS
		${CMAKE_SOURCE_DIR}/build/resources/0ad.sh
	DESTINATION
		${CMAKE_INSTALL_BINDIR}
	COMPONENT	resources
)

install(
	FILES
		${CMAKE_SOURCE_DIR}/build/resources/0ad.png
	DESTINATION
		${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
	COMPONENT	resources
)
