target_sources(tinygettext
	PRIVATE
		dictionary.cpp
		dictionary_manager.cpp
		iconv.cpp
		language.cpp
		log.cpp
		plural_forms.cpp
		po_parser.cpp
		tinygettext.cpp
		unix_file_system.cpp
		$<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/win32/dirent.c>
		$<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/win32/dirent.h>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/win32/dirent.c
		TARGET_DIRECTORY tinygettext # Must be set otherwise cmake does not apply it correctly
		PROPERTIES LANGUAGE CXX
	)
endif()
