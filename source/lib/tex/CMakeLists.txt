target_sources(lowlevel
	PRIVATE
		tex.cpp
		tex.h
		tex_bmp.cpp
		tex_codec.cpp
		tex_codec.h
		tex_dds.cpp
		tex_internal.h
		tex_png.cpp
		tex_tga.cpp
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
