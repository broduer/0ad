add_library(gladwrapper STATIC)

target_link_libraries(gladwrapper
	PRIVATE
		${BUILD_FLAGS_TARGET}
)

target_sources(gladwrapper
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan.cpp
		$<$<BOOL:${gles}>:${CMAKE_CURRENT_SOURCE_DIR}/src/gles2.cpp>
		$<$<NOT:$<BOOL:${gles}>>:${CMAKE_CURRENT_SOURCE_DIR}/src/gl.cpp>
)

target_include_directories(gladwrapper
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(gladwrapper
	PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:/wd4551>
)

disable_rtti(gladwrapper)
