target_sources(engine
	PRIVATE
		XMBData.cpp
		XMBData.h
		XMBStorage.cpp
		XMBStorage.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
