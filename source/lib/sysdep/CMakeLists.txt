
target_sources(lowlevel
	PRIVATE
		arch.h
		compiler.h
		cpu.cpp
		cpu.h
		dir_watch.h
		filesystem.h
		os.h
		os_cpu.h
		rtl.h
		stl.h
		sysdep.h
		vm.h
)

add_subdirectory(arch/)
add_subdirectory(os/)
add_subdirectory(rtl/)
if(NOT without-tests)
	add_subdirectory(tests/)
endif()
