target_sources(lowlevel
	PRIVATE
		file.cpp
		file.h
		file_system.cpp
		file_system.h
)

add_subdirectory(archive/)
add_subdirectory(common/)
# add_subdirectory(disabled_tests/)
add_subdirectory(io/)
add_subdirectory(vfs/)
