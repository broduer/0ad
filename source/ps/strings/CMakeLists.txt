target_sources(engine
	PRIVATE
		StringBuilder.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
