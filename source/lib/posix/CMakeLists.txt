target_sources(lowlevel
	PRIVATE
		posix.cpp
		posix.h
		posix_aio.h
		posix_dlfcn.h
		posix_filesystem.h
		posix_mman.h
		posix_pthread.h
		posix_types.h
		posix_utsname.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
