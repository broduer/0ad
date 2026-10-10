cmake_minimum_required(VERSION 3.25.1...4.0.0)

project(pyrogenesis VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES CXX)

include(0ad-Functions)
# +++++++++++++++++++ Get Dependencies ++++++++++++++++++++++++++++++++++++++++
if(UNIX)
	find_package(SDL2 2.0.2 REQUIRED)
	find_package(Boost 1.69.0 REQUIRED CONFIG)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	include(FindPrebuildLibrary)
	find_prebuild_library(Sdl2)
	find_prebuild_library(Boost INC_PATH ${0AD_EXT_LIBDIR}/boost/include/)
endif()
# find_package(nlohmann_json 3.12 CONFIG REQUIRED)
find_package(LibXml2 2.9 REQUIRED)
find_package(Iconv 1.0 REQUIRED)
find_package(ICU 72.1 COMPONENTS i18n uc REQUIRED)
find_package(ZLIB 1.2 REQUIRED)
find_package(CxxTest 4.4 REQUIRED)
find_package(CURL 7.32 REQUIRED)
find_package(PNG 0.57 REQUIRED)
find_package(Freetype 2.12 REQUIRED)
find_package(OpenAL 3.18 REQUIRED)
if(UNIX)
	set(THREADS_PREFER_PTHREAD_FLAG TRUE)
endif()
find_package(Threads REQUIRED)
find_package(ENet 1.3 REQUIRED)
if(with-system-mozjs)
	find_package(Mozjs 128 REQUIRED)
else()
	find_package(BundledMozjs 128 REQUIRED)
endif()
if(with-system-nvtt)
	find_package(NVTT REQUIRED)
else()
	find_package(BundledNVTT REQUIRED)
endif()
find_package(fmt 9.1.0 REQUIRED)
find_package(Gloox 1.0.20 REQUIRED)
find_package(Sodium 1.0.18 REQUIRED)
find_package(Vorbis 1.3.7 REQUIRED)
if(with-system-cpp-httplib OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
	find_package(CppHTTPlib 0.30.1 REQUIRED)
endif()
if(NOT without-miniupnpc)
	find_package(Miniupnpc 2.2.8 REQUIRED)
endif()
if(with-valgrind AND UNIX)
	find_package(Valgrind 3.19.0 REQUIRED)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	find_library(COMSUPPW NAMES comsuppw REQUIRED)
	find_library(OLEAUT32 NAMES oleaut32 REQUIRED)
endif()

# +++++++++++++++++++ Create main targets +++++++++++++++++++++++++++++++++++++
add_executable(${PROJECT_NAME})
add_library(engine STATIC)
add_library(graphics STATIC)
add_library(atlas STATIC)
add_library(lowlevel STATIC)

# +++++++++++++++++++ Add sub libraries +++++++++++++++++++++++++++++++++++++++++++++++
if(NOT without-dap-interface)
	add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/dapinterface)
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/graphics)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/gui)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/i18n)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lib)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lobby)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/maths)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/network)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/ps)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/renderer)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/rlinterface)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/scriptinterface)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/simulation2)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/soundmanager)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/tools/atlas/GameInterface)

# +++++++++++++++++++ Linking and other settings ++++++++++++++++++++++++++++++
# ------- Engine -------
target_include_directories(engine PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_link_libraries(engine
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		SDL2::SDL2
		LibXml2::LibXml2
		ZLIB::ZLIB
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		ENet::ENet
		CURL::libcurl
		tinygettext
		ICU::i18n
		ICU::uc
		Iconv::Iconv
		Sodium::Sodium
		PNG::PNG
		fmt::fmt
		Freetype::Freetype
		$<IF:$<OR:$<BOOL:${with-system-cpp-httplib}>,$<PLATFORM_ID:Windows>>,CppHTTPlib::CppHTTPlib,httplib>
		$<$<NOT:$<BOOL:${without-lobby}>>:Gloox::Gloox>
		$<$<NOT:$<BOOL:${without-miniupnpc}>>:Miniupnpc::Miniupnpc>
		$<$<NOT:$<BOOL:${without-nvtt}>>:$<IF:$<BOOL:${with-system-nvtt}>,nvtt::nvtt,BdlNVTT::BdlNVTT>>
		$<$<NOT:$<BOOL:${without-audio}>>:OpenAL::OpenAL>
		$<$<NOT:$<BOOL:${without-audio}>>:Vorbis::Vorbis>
)
disable_rtti(engine)
if(NOT without-pch)
	add_pch(TARGET engine PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/engine)
else()
	target_compile_definitions(engine PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(engine PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

# ------- graphics -------
target_include_directories(graphics PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_link_libraries(graphics
	PRIVATE
		${BUILD_FLAGS_TARGET}
		SDL2::SDL2
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		fmt::fmt
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		Freetype::Freetype
		ICU::i18n
		ICU::uc
		LibXml2::LibXml2
		Iconv::Iconv
		$<$<NOT:$<BOOL:${without-nvtt}>>:$<IF:$<BOOL:${with-system-nvtt}>,nvtt::nvtt,BdlNVTT::BdlNVTT>>
		gladwrapper
)
disable_rtti(graphics)
if(NOT without-pch)
	add_pch(TARGET graphics PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/graphics)
else()
	target_compile_definitions(graphics PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(graphics PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

# ------- Atlas -------
target_include_directories(atlas PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(atlas
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		SDL2::SDL2
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		fmt::fmt
		LibXml2::LibXml2
		Iconv::Iconv
)
disable_rtti(atlas)
if(NOT without-pch)
	add_pch(TARGET atlas PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/atlas)
else()
	target_compile_definitions(atlas PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(atlas PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

# ------- Lowlevel -------
target_include_directories(lowlevel
	PRIVATE
	${CMAKE_SOURCE_DIR}/source
	${CXXTEST_INCLUDE_DIRS}
)
target_link_libraries(lowlevel
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		SDL2::SDL2
		OpenAL::OpenAL
		PNG::PNG
		ZLIB::ZLIB
		$<$<AND:$<BOOL:${with-valgrind}>,$<PLATFORM_ID:Linux,Darwin,FreeBSD>>:Valgrind::Valgrind>
		fmt::fmt
		gladwrapper
)
target_compile_definitions(lowlevel
	PRIVATE
		$<$<AND:$<BOOL:${with-valgrind}>,$<PLATFORM_ID:Linux,Darwin,FreeBSD>>:CONFIG2_VALGRIND>
)
disable_rtti(lowlevel)
if(NOT without-pch)
	add_pch(TARGET lowlevel PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/lowlevel)
else()
	target_compile_definitions(lowlevel PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(lowlevel PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

# +++++++++++++++++++ Main target +++++++++++++++++++++++++++++++++++++++++++++
# Necessary for linkgroups. Exclude INTERFACE libraries like the build-flags and Iconv
set(MAIN_TARGET_LIBS
	SDL2::SDL2
	PNG::PNG
	ZLIB::ZLIB
	$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
	LibXml2::LibXml2
	$<IF:$<OR:$<BOOL:${with-system-cpp-httplib}>,$<PLATFORM_ID:Windows>>,CppHTTPlib::CppHTTPlib,httplib>
	$<$<PLATFORM_ID:Windows>:${COMSUPPW}>
	ENet::ENet
	CURL::libcurl
	tinygettext
	ICU::i18n
	ICU::uc
	Sodium::Sodium
	fmt::fmt
	Freetype::Freetype
	$<$<AND:$<BOOL:${with-valgrind}>,$<PLATFORM_ID:Linux,Darwin,FreeBSD>>:Valgrind::Valgrind>
	$<$<PLATFORM_ID:Windows>:${OLEAUT32}>
	$<$<NOT:$<BOOL:${without-audio}>>:OpenAL::OpenAL>
	$<$<NOT:$<BOOL:${without-audio}>>:Vorbis::Vorbis>
	$<$<NOT:$<BOOL:${without-nvtt}>>:$<IF:$<BOOL:${with-system-nvtt}>,nvtt::nvtt,BdlNVTT::BdlNVTT>>
	$<$<NOT:$<BOOL:${without-lobby}>>:Gloox::Gloox>
	$<$<NOT:$<BOOL:${without-miniupnpc}>>:Miniupnpc::Miniupnpc>
	$<$<AND:$<BOOL:${link_execinfo}>,$<PLATFORM_ID:Linux,FreeBSD>>:execinfo>
	$<$<PLATFORM_ID:Windows>:delayimp>
	$<$<PLATFORM_ID:Darwin>:ApplicationServices.framework>
	$<$<PLATFORM_ID:Darwin>:Cocoa.framework>
	$<$<PLATFORM_ID:Darwin>:CoreFoundation.framework>
	${CMAKE_DL_LIBS}
	# Internal static libraries
	network
	rlinterface
	$<$<NOT:$<BOOL:${without-miniupnpc}>>:dapinterface>
	$<$<NOT:$<BOOL:${without-lobby}>>:lobby>
	simulation2
	scriptinterface
	engine
	graphics
	atlas
	gui
	lowlevel
	gladwrapper
)
target_sources(${PROJECT_NAME}
	PRIVATE
		main.cpp
)
target_include_directories(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_LIST_DIR}
		${CXXTEST_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME}
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		Iconv::Iconv
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	target_link_libraries(${PROJECT_NAME} PRIVATE ${MAIN_TARGET_LIBS})
else()
	target_link_libraries(${PROJECT_NAME} PRIVATE $<LINK_GROUP:RESCAN,${MAIN_TARGET_LIBS}>)
endif()
target_link_options(${PROJECT_NAME}
	PRIVATE
		# wraps main thread in a __try block(see wseh.cpp). replace with mainCRTStartup if that's undesired.
		$<$<PLATFORM_ID:Windows>:/ENTRY:wseh_EntryPoint>
		# NDK's STANDALONE-TOOLCHAIN.html says this is required
		$<$<PLATFORM_ID:Android>:-Wl,--fix-cortex-a8>
		# allow the executable to use more than 2GB of RAM.
		$<$<AND:$<PLATFORM_ID:Windows>,$<STREQUAL:${ARCH},x86>>:/LARGEADDRESSAWARE>
		$<$<AND:$<PLATFORM_ID:Windows>,$<STREQUAL:${ARCH},amd64>>:/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df'>
		$<$<AND:$<PLATFORM_ID:Windows>,$<STREQUAL:${ARCH},x86>>:/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='X86' publicKeyToken='6595b64144ccf1df'>
		$<$<AND:$<CONFIG:Debug>,$<PLATFORM_ID:Linux,FreeBSD>>:-rdynamic>
)
target_compile_definitions(${PROJECT_NAME}
	PRIVATE
		$<$<AND:$<BOOL:${with-valgrind}>,$<PLATFORM_ID:Linux,Darwin,FreeBSD>>:CONFIG2_VALGRIND>
)
disable_rtti(${PROJECT_NAME})

set_target_properties(${PROJECT_NAME}
	PROPERTIES
		ENABLE_EXPORTS TRUE
		# To use our local shared libraries, they need to be found in the runtime dynamic linker path. Add their path to -rpath.
		INSTALL_RPATH $<IF:$<BOOL:${libdir}>,${libdir},$ORIGIN>
		BUILD_WITH_INSTALL_RPATH TRUE
		XCODE_ATTRIBUTE_ARCHS ${MACOS_ARCH}
		XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET $<$<BOOL:${macosx-version-min}>:${macosx-version-min}>
		WIN32_EXECUTABLE $<$<PLATFORM_ID:Windows>:TRUE>
		MACOSX_BUNDLE $<$<PLATFORM_ID:Darwin>:TRUE>
)

add_dependencies(${PROJECT_NAME} Collada)

# +++++++++++++++++++ Install section +++++++++++++++++++++++++++++++++++++++++
include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
