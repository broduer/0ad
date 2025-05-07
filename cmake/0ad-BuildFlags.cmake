cmake_minimum_required(VERSION 3.25.1...4.0.0)

# Add an interface library with common set of build/linker flags and definitions.

set(BUILD_FLAGS_TARGET BuildFlags)
add_library(${BUILD_FLAGS_TARGET} INTERFACE)

# Enable and require C++20 standard.
target_compile_features(${BUILD_FLAGS_TARGET} INTERFACE cxx_std_20)
set_target_properties(${BUILD_FLAGS_TARGET}
	PROPERTIES
		CXX_STANDARD_REQUIRED	ON
		CXX_EXTENSIONS			OFF
)

# Settings for build types
if(with-lto)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT lto_supported OUTPUT lto_supported_error)
	if(lto_supported)
		set_target_properties(${BUILD_FLAGS_TARGET}
			PROPERTIES
				INTERPROCEDURAL_OPTIMIZATION TRUE
		)
	else()
		message(WARNING "IPO / LTO not supported: <${lto_supported_error}>")
	endif()
	unset(lto_supported)
	unset(lto_supported_error)
endif()
target_compile_definitions(${BUILD_FLAGS_TARGET}
	INTERFACE
		$<$<CONFIG:Release>:NDEBUG>
		$<$<CONFIG:Release>:CONFIG_FINAL=1>
		$<$<CONFIG:Debug>:DEBUG>
		# Game Configuration Defines
		$<$<BOOL:${mozjs_is_debug_build}>:DEBUG>
		$<$<BOOL:${gles}>:CONFIG2_DAP_INTERFACE=0>
		$<$<BOOL:${without-audio}>:CONFIG2_GLES=1>
		$<$<BOOL:${without-nvtt}>:CONFIG2_NVTT=0>
		$<$<BOOL:${without-lobby}>:CONFIG2_LOBBY=0>
		$<$<BOOL:${without-miniupnpc}>:CONFIG2_MINIUPNPC=0>
		$<$<BOOL:${without-dap-interface}>:CONFIG2_DAP_INTERFACE=0>
)

# Address Sanitizer Settings
if(sanitize-address)
	target_compile_options(${BUILD_FLAGS_TARGET} INTERFACE -fsanitize=address)
	target_link_options(${BUILD_FLAGS_TARGET} INTERFACE -fsanitize=address)
endif()
if(sanitize-thread)
	target_compile_options(${BUILD_FLAGS_TARGET} INTERFACE -fsanitize=thread)
	target_link_options(${BUILD_FLAGS_TARGET} INTERFACE -fsanitize=thread)
endif()
if(sanitize-undefined-behaviour)
	target_compile_options(${BUILD_FLAGS_TARGET} INTERFACE -fsanitize=undefined)
	target_link_options(${BUILD_FLAGS_TARGET} INTERFACE -fsanitize=undefined)
endif()

# hide warnings caused by library includes (Nothing to do for gcc/clang)
target_compile_options(${BUILD_FLAGS_TARGET}
		INTERFACE
			$<$<CXX_COMPILER_ID:MSVC>:/external:W0>
)

# various platform-specific build flags
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	target_compile_options(${BUILD_FLAGS_TARGET}
		INTERFACE
			$<$<CXX_COMPILER_ID:MSVC>:/MP>
			# Since KB4088875 Windows 7 has a soft requirement for SSE2.
			# Windows 8+ and Firefox ESR52 make it hard requirement.
			# Finally since VS2012 it's enabled implicitely when not set.
			$<$<CXX_COMPILER_ID:MSVC>:/arch:SSE2>
			# use native wchar_t type (not typedef to unsigned short)
			$<$<CXX_COMPILER_ID:MSVC>:/Zc:wchar_t>
			$<$<CXX_COMPILER_ID:MSVC>:/utf-8>
			# SpiderMonkey only supports building with MSVC on a best-effort basis,
			# and the traditional MSVC preprocessor is incompatible with some headers.
			# Use the modern, standard-compliant MSVC preprocessor instead.
			$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
			# enable most of the standard warnings
			$<$<CXX_COMPILER_ID:MSVC>:/W4>
			# FIXME: conversion warnings, should add -Wconversion to gcc and clang flags as well
			$<$<CXX_COMPILER_ID:MSVC>:/wd4267>
			$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
	)

	# disable LNK4221 warning, to avoid spending energy ordering projects in linker invocations
	target_link_options(${BUILD_FLAGS_TARGET}
		INTERFACE
			"/ignore:4221"
	)

	# mozilla 115 linked list destructor in debug build
	target_compile_definitions(${BUILD_FLAGS_TARGET} INTERFACE "__PRETTY_FUNCTION__=__FUNCSIG__")

	# disable Windows debug heap, since it makes malloc/free hugely slower when running inside a debugger
	set_target_properties(${BUILD_FLAGS_TARGET}
		PROPERTIES
			VS_DEBUGGER_ENVIRONMENT "_NO_DEBUG_HEAP=1"
	)

elseif(UNIX)
	if(NOT minimal-flags)
		target_compile_options(${BUILD_FLAGS_TARGET}
			INTERFACE
				# most of the standard warnings
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall>
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wextra>
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wpedantic>
				# $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wconversion>								# FIXME: should seriously consider fixing so this warning can be enabled.
				# add some other useful warnings that need to be enabled explicitly
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wunused-parameter>							# (useful for finding some multiply-included header files)
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wredundant-decls>
				# $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wformat=2>									# (useful sometimes, but a bit noisy, so skip it by default)
				# $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wcast-qual>								# (useful for checking const-correctness, but a bit noisy, so skip it by default)
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wnon-virtual-dtor>							# (sometimes noisy but finds real bugs)
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wundef>										# (useful for finding macro name typos)
				# disable some warnings that currently trigger
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wno-missing-field-initializers>				# (this is common in external headers we can't fix)
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wno-reorder>									# order of initialization list in constructors (lots of noise)
				# enable security features (stack checking etc) that shouldn't have a significant effect on performance and can catch bugs
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fstack-protector-strong>
				# always enable strict aliasing (useful in debug builds because of the warnings)
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fstrict-aliasing>
				# don't omit frame pointers (for now), because performance will be impacted negatively by the way this breaks profilers more than it will be impacted positively by the optimisation
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fno-omit-frame-pointer>
				# FORTIFY_SOURCE needs optimizations to be enabled
				$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang,AppleClang,GNU>>:-U_FORTIFY_SOURCE>	# (avoid redefinition warning if already defined)
				$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang,AppleClang,GNU>>:-D_FORTIFY_SOURCE=2>
		)
		target_link_options(${BUILD_FLAGS_TARGET}
			INTERFACE
				$<$<PLATFORM_ID:Darwin>:-multiply_defined>
				$<$<PLATFORM_ID:Darwin>:suppress>
		)

		if(NOT without-pch)
			# do something (?) so that ccache can handle compilation with PCH enabled (ccache 3.1+ also requires CCACHE_SLOPPINESS=time_macros for this to work)
			target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fpch-preprocess>
			)
		endif()

		if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_NAME MATCHES ".*BSD*")
			target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fPIC>
			)
			target_link_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					"-Wl,--no-undefined"
					"-Wl,--as-needed"
					"-Wl,-z,relro"
			)
		endif()

		if(ARCH STREQUAL "x86")
		# To support intrinsics like __sync_bool_compare_and_swap on x86 we need to set -march to something that supports them (i686).
		# We use pentium3 to also enable other features like mmx and sse, while tuning for generic to have good performance on every supported CPU.
		# Note that all these features are already supported on amd64.
			target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-march=pentium3>
					$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-mtune=generic>
					# This allows x86 operating systems to handle the 2GB+ INTERFACE mod.
					$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-mtune=generic-D_FILE_OFFSET_BITS=64>
			)
		endif()

		if(ARCH STREQUAL "arm")
			target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					# disable warnings about va_list ABI change and use compile-time flags for futher configuration.
					$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wno-psabi>
					# Android uses softfp, so we should too.
					$<$<AND:$<CXX_COMPILER_ID:Clang,AppleClang,GNU>,$<BOOL:${android}>>:-mfloat-abi=softfp>
			)
		endif()
	endif()

	if(coverage)
		target_compile_options(${BUILD_FLAGS_TARGET}
			INTERFACE
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fprofile-arcs>
				$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-ftest-coverage>
		)
		target_link_libraries(${BUILD_FLAGS_TARGET} gcov)
	endif()

	# MacOS 10.12 only supports intel processors with SSE 4.1, so enable that.
	if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND ARCH STREQUAL "amd64")
		target_compile_options(${BUILD_FLAGS_TARGET}
			INTERFACE
				"-msse4.1"
		)
	endif()

	# Check if SDK path should be used
	if(sysroot)
		target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					"-isysroot ${sysroot}"
		)
		target_link_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					"-Wl,-syslibroot, ${sysroot}"
		)
	endif()

	# On OS X, sometimes we need to specify the minimum API version to use
	if(macosx-version-min)
		# clang and llvm-gcc look at mmacosx-version-min to determine link target and CRT version, and use it to set the macosx_version_min linker flag
		target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					"-mmacosx-version-min=${macosx-version-min}"
		)
		target_link_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					"-mmacosx-version-min=${macosx-version-min}"
		)
	endif()

	# Only libc++ is supported on MacOS
	if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		target_compile_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					-stdlib=libc++
		)
		target_link_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					-stdlib=libc++
		)
	endif()

	# Hide symbols in dynamic shared objects by default, for efficiency and for equivalence with Windows
	# - they should be exported explicitly with __attribute__ ((visibility ("default")))
	target_compile_options(${BUILD_FLAGS_TARGET}
		INTERFACE
			-fvisibility=hidden
	)

	target_compile_definitions(${BUILD_FLAGS_TARGET}
		INTERFACE
			$<$<BOOL:${bindir}>:INSTALLED_BINDIR=${bindir}>
			$<$<BOOL:${datadir}>:INSTALLED_DATADIR=${bindir}>
			$<$<BOOL:${libdir}>:INSTALLED_LIBDIR=${bindir}>
	)

	# RPATH Settings. Must be done through INSTALL_RPATH on target/global base.
	if(CMAKE_SYSTEM_NAME MATCHES ".*BSD*")
		# On FreeBSD we need to allow use of $ORIGIN
		target_link_options(${BUILD_FLAGS_TARGET}
				INTERFACE
					-Wl,-z,origin
		)
	endif()
endif()
