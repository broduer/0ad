# +++++++++++++++++++++ Fetch libraries ++++++++++++++++++++
include(FetchContent)

if(NOT with-system-cpp-httplib)
	FetchContent_Declare(cpp-httplib
		GIT_REPOSITORY	https://github.com/yhirose/cpp-httplib
		GIT_TAG			v0.30.1
	)
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
	URL			https://github.com/KhronosGroup/SPIRV-Reflect/archive/refs/tags/vulkan-sdk-1.3.290.0.tar.gz
	URL_HASH	SHA512=5de92dbd04424fb09f6c2a4c890572b2789017b737822d5bc8ab713aeede0796cdcc7ffe74e3ffe25cdd4c626e94bd60757e1cc625ede02b8fe3994b07588b44
)
FetchContent_MakeAvailable(spirv-reflect)
