target_sources(graphics
	PRIVATE
		OgreGLSLPreprocessor.cpp
		OgreGLSLPreprocessor.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
