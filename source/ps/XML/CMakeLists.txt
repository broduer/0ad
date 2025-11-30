target_sources(engine
	PRIVATE
		RelaxNG.cpp
		RelaxNG.h
		XMLWriter.cpp
		XMLWriter.h
		Xeromyces.cpp
		Xeromyces.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
