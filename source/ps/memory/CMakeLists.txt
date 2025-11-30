target_sources(engine
	PRIVATE
		LinearAllocator.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
