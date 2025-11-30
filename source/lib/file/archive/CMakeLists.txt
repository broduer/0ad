target_sources(lowlevel
	PRIVATE
		archive.cpp
		archive.h
		archive_zip.cpp
		archive_zip.h
		codec.cpp
		codec.h
		codec_zlib.cpp
		codec_zlib.h
		stream.cpp
		stream.h
)

# add_subdirectory(disabled_tests/)
if(NOT without-tests)
	add_subdirectory(tests/)
endif()
