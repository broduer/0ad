# +++++++++++++++++++++ Fetch libraries ++++++++++++++++++++
include(FetchContent)

if(NOT with-system-cpp-httplib)
	FetchContent_Declare(cpp-httplib
		GIT_REPOSITORY	https://github.com/yhirose/cpp-httplib
		GIT_TAG			v0.30.1
	)
	set(HTTPLIB_INSTALL OFF) # Neccesary to avoid installing httplib additionaly
	set(HTTPLIB_COMPILE YES)
	set(HTTPLIB_USE_BROTLI_IF_AVAILABLE NO)
	set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE NO)
	set(HTTPLIB_USE_ZLIB_IF_AVAILABLE NO)
	set(HTTPLIB_USE_ZSTD_IF_AVAILABLE NO)
	FetchContent_MakeAvailable(cpp-httplib)
endif()

if(NOT with-system-cxxtest)
	# Only fetch cxxtest and append to CMAKE_PREFIX_PATH to override searching.
	FetchContent_Populate(cxxtest
		GIT_REPOSITORY	https://github.com/CxxTest/cxxtest.git
		GIT_TAG			4.4
		PATCH_COMMAND	git restore <SOURCE_DIR>
		COMMAND			git apply ${CMAKE_SOURCE_DIR}/libraries/source/cxxtest-4.4/patches/0001-Add-Debian-python3-patch.patch
	)
	list(APPEND CMAKE_PREFIX_PATH ${cxxtest_SOURCE_DIR})
endif()

if(NOT with-system-nvtt)
	FetchContent_Declare(nvtt
		SVN_REPOSITORY	https://svn.wildfiregames.com/public/source-libs/trunk/nvtt
		SVN_REVISION	-r28209
		PATCH_COMMAND	svn revert --recursive <SOURCE_DIR>
		COMMAND			svn patch ${CMAKE_SOURCE_DIR}/libraries/source/nvtt/patches/0001-Don-t-overspecify-flags.patch
		COMMAND			svn patch ${CMAKE_SOURCE_DIR}/libraries/source/nvtt/patches/0002-Bump-cmake-min-version-to-3.10.patch
		COMMAND			svn patch ${CMAKE_SOURCE_DIR}/libraries/source/nvtt/patches/0003-Use-execute_process-insted-of-exec_program.patch
		COMMAND			svn patch ${CMAKE_SOURCE_DIR}/libraries/source/nvtt/patches/0004-Properly-detect-ppc64le-systems.patch
		COMMAND			svn patch ${CMAKE_SOURCE_DIR}/libraries/source/nvtt/patches/0005-Fix-compiler-flags-on-ppc64le-systems.patch
		COMMAND			svn patch ${CMAKE_SOURCE_DIR}/libraries/source/nvtt/patches/0006-Fix-altivec-include-on-ppc64le-systems.patch
	)
	set(NVTT_SHARED YES)
	set(OpenGL_GL_PREFERENCE GLVND)
	if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
		set(PNG NO)
	endif()
	FetchContent_MakeAvailable(nvtt)
endif()

FetchContent_Declare(spirv-reflect
	URL			https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
	URL_HASH	SHA512=93BBD834585C027BD9798FE24271CF1225B670F33EB6772F729C0B7ABE0B0BE8BF08B9D02604F88DB7F033C138E62560A986354DD1C48B77451FF77F01A7FDAF
)
set(SPIRV_REFLECT_INSTALL OFF)
FetchContent_MakeAvailable(spirv-reflect)

# +++++++++++++++++++++ External Projects ++++++++++++++++++++
include(ExternalProject)
include(ProcessorCount)
ProcessorCount(N)

ExternalProject_Add(fcollada
	SVN_REPOSITORY		https://svn.wildfiregames.com/public/source-libs/trunk/fcollada
	SVN_REVISION		-r28209
	CONFIGURE_COMMAND	""
	BUILD_COMMAND		make -C <SOURCE_DIR>/src/ -j${N} output/libFColladaSD.a output/libFColladaSR.a
	USES_TERMINAL_BUILD	TRUE
	INSTALL_COMMAND		make -C <SOURCE_DIR>/src/ install
	EXCLUDE_FROM_ALL	TRUE
)

if(NOT with-system-mozjs)
	set(MOZJS_VERSION 128.13.0)
	list(APPEND MOZJS_ENV_VARS
		MOZ_NOSPAM=1
		# Do not let mach fetch python and rust packages online: use vendored content
		MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE=none
		MOZBUILD_STATE_PATH="${CMAKE_BINARY_DIR}/mozbuild-state"
		BUILD_DIR="${CMAKE_BINARY_DIR}/mozjs-release"
	)
	list(APPEND MACH_OPTIONS
		--enable_profiling
		--enable-perf
		--enable-jitspew
		--with-jitreport-granularity=3
		--enable-optimize
	)
	message(STATUS ${MOZJS_ENV_VARS})
	ExternalProject_Add(mozjs
		URL					https://releases.wildfiregames.com/libs/mozjs-${MOZJS_VERSION}.tar.xz
		URL_HASH			SHA256=6422e51b9e76586c3054127bf13f992ec2338bd2d6156dc430accfb0b5301f1d
		PATCH_COMMAND		${CMAKE_SOURCE_DIR}/libraries/source/spidermonkey/patches/patch.sh
		CONFIGURE_COMMAND	""
		BUILD_COMMAND		${CMAKE_COMMAND} -E env ${MOZJS_ENV_VARS} <SOURCE_DIR>/mach build ${MACH_OPTIONS}
		USES_TERMINAL_BUILD	TRUE
		INSTALL_COMMAND		""
		EXCLUDE_FROM_ALL	TRUE
	)
endif()

unset(N)
