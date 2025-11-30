target_sources(engine
	PRIVATE
		StaticVector.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
