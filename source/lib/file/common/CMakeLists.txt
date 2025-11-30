target_sources(lowlevel
	PRIVATE
		file_loader.cpp
		file_loader.h
		file_stats.cpp
		file_stats.h
		real_directory.cpp
		real_directory.h
		trace.cpp
		trace.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
