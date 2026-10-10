cmake_minimum_required(VERSION 3.25.1...4.0.0)

project(AtlasMisc LANGUAGES CXX)

include(0ad-Functions)

# +++++++++++++++++++++ Project Macros ++++++++++++++++++++
macro(set_atlas_build_flags _target)
	target_link_options(${_target}
		PRIVATE
			$<$<PLATFORM_ID:Linux,FreeBSD>:-fPIC>
			$<$<PLATFORM_ID:Linux>:-rdynamic>
	)
	target_compile_options(${_target}
		PRIVATE
			$<$<PLATFORM_ID:Linux,FreeBSD>:-fPIC>
			$<$<PLATFORM_ID:Linux,Darwin>:-Wno-unused-local-typedefs>
	)
	target_link_libraries(${_target}
		PRIVATE
			$<$<PLATFORM_ID:Windows>:winmm delayimp>
	)
endmacro()

# +++++++++++++++++++++ Required Packages ++++++++++++++++++
find_package(wxWidgets 3.0.4 REQUIRED COMPONENTS gl xml)
find_package(Iconv 1.0 REQUIRED)
find_package(ZLIB 1.2 REQUIRED)
find_package(LibXml2 2.9 REQUIRED)
find_package(CxxTest 4.4 REQUIRED)
if(UNIX)
	find_package(Boost 1.69.0 REQUIRED CONFIG)
	find_package(SDL2 2.0.2 REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	include(FindPrebuildLibrary)
	find_prebuild_library(Sdl2)
	find_prebuild_library(Boost INC_PATH ${0AD_EXT_LIBDIR}/boost/include/)
endif()

# +++++++++++++++++++++ Project Targets ++++++++++++++++++++
add_library(AtlasObject STATIC)
add_library(AtlasUI SHARED)
add_executable(ActorEditor)

# Set Macos Bundle and Windows Window Application
set_target_properties(ActorEditor
	PROPERTIES
		WIN32_EXECUTABLE $<$<PLATFORM_ID:Windows>:TRUE>
		MACOSX_BUNDLE $<$<PLATFORM_ID:Darwin>:TRUE>
)

add_subdirectory(AtlasFrontends/)
add_subdirectory(AtlasObject/)
add_subdirectory(AtlasUI/)

# +++++++++++++++++++++ Set Output Paths +++++++++++++++++++
set_target_properties(AtlasObject
	PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries/system
)
set_target_properties(AtlasUI
	PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries/system
)
set_target_properties(ActorEditor
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/binaries/system
		BUILD_WITH_INSTALL_RPATH TRUE
)

# +++++++++++++++++++++ AtlasObject ++++++++++++++++++++++++
set_atlas_build_flags(AtlasObject)
target_include_directories(AtlasObject PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_link_libraries(AtlasObject
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		Iconv::Iconv
		LibXml2::LibXml2
		SDL2::SDL2
)

# +++++++++++++++++++++ AtlasUI ++++++++++++++++++++++++++++
if(NOT without-pch)
	add_pch(TARGET AtlasUI PCH_DIR ${PROJECT_SOURCE_DIR}/AtlasUI/Misc)
else()
	target_compile_definitions(AtlasUI PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(AtlasUI PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
	target_include_directories(AtlasUI
		BEFORE PRIVATE
			${PROJECT_SOURCE_DIR}/AtlasUI/Misc
	)
endif()

set_atlas_build_flags(AtlasUI)

target_include_directories(AtlasUI PRIVATE ${CMAKE_SOURCE_DIR}/source/)
target_link_libraries(AtlasUI
	PRIVATE
		${BUILD_FLAGS_TARGET}
		LibXml2::LibXml2
		Iconv::Iconv
		$<$<PLATFORM_ID:Linux>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		SDL2::SDL2
		ZLIB::ZLIB
		AtlasObject
		wxWidgets::wxWidgets
)
target_compile_definitions(AtlasUI
	PRIVATE
		$<$<VERSION_GREATER_EQUAL:${wxWidgets_VERSION},3.3.0>:wxNO_REQUIRE_LITERAL_MSGIDS>
)

# +++++++++++++++++++++ ActorEditor ++++++++++++++++++++++++
target_include_directories(ActorEditor
	PRIVATE
		${PROJECT_SOURCE_DIR}/
)
target_link_libraries(ActorEditor
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<NOT:$<PLATFORM_ID:Windows>>:AtlasObject>
		AtlasUI
)
target_link_options(ActorEditor
	PRIVATE
		$<$<AND:$<PLATFORM_ID:Windows>,$<STREQUAL:${ARCH},amd64>>:/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df'>
		$<$<AND:$<PLATFORM_ID:Windows>,$<STREQUAL:${ARCH},x86>>:/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='X86' publicKeyToken='6595b64144ccf1df'>
		$<$<PLATFORM_ID:Darwin>:-arch ${MACOS_ARCH}>
)
target_compile_options(ActorEditor
	PRIVATE
		$<$<PLATFORM_ID:Darwin>:-arch ${MACOS_ARCH}>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set_target_properties(ActorEditor PROPERTIES
		XCODE_ATTRIBUTE_ARCHS ${MACOS_ARCH}
		XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET $<$<BOOL:${macosx-version-min}>:${macosx-version-min}>
	)
endif()

# +++++++++++++++++++ Install section +++++++++++++++++++++++++++++++++++++++++
include(GNUInstallDirs)

install(TARGETS AtlasUI
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/0ad
)

install(TARGETS ActorEditor
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
