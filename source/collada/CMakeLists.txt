cmake_minimum_required(VERSION 3.25.1...4.0.0)

project(Collada VERSION 0.0.0 LANGUAGES CXX)

find_package(FCollada 3.5 REQUIRED)
find_package(LibXml2 2.9 REQUIRED)
find_package(Iconv 1.0 REQUIRED)

add_library(${PROJECT_NAME} MODULE)

target_sources(${PROJECT_NAME}
	PRIVATE
		CommonConvert.cpp
		CommonConvert.h
		DLL.cpp
		DLL.h
		Decompose.cpp
		Decompose.h
		GeomReindex.cpp
		GeomReindex.h
		Maths.cpp
		Maths.h
		PMDConvert.cpp
		PMDConvert.h
		PSAConvert.cpp
		PSAConvert.h
		StdSkeletons.cpp
		StdSkeletons.h
		XMLFix.cpp
		XMLFix.h
		precompiled.cpp
		precompiled.h
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		${BUILD_FLAGS_TARGET}
		FCollada::FCollada
		Iconv::Iconv
		LibXml2::LibXml2
		${CMAKE_DL_LIBS}
)

target_compile_definitions(${PROJECT_NAME}
	PRIVATE
		$<$<PLATFORM_ID:Linux>:LINUX>
)

target_compile_options(${PROJECT_NAME}
	PRIVATE
		# FCollada is not aliasing-safe, so disallow dangerous optimisations
		# (TODO: It'd be nice to fix FCollada, but that looks hard)
		$<$<PLATFORM_ID:Linux,FreeBSD,Darwin>:-fno-strict-aliasing>
		$<$<PLATFORM_ID:Darwin>:-arch ${MACOS_ARCH}>
)

target_link_options(${PROJECT_NAME}
	PRIVATE
		$<$<PLATFORM_ID:Linux,FreeBSD>:-rdynamic>
		# On OSX, fcollada uses a few utility functions from coreservices
		$<$<PLATFORM_ID:Darwin>:CoreServices.framework>
		$<$<PLATFORM_ID:Darwin>:-arch ${MACOS_ARCH}>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set_target_properties(${PROJECT_NAME} PROPERTIES
		XCODE_ATTRIBUTE_ARCHS ${MACOS_ARCH}
	)
endif()
