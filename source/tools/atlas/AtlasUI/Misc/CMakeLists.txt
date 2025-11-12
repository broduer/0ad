target_sources(AtlasUI
	PRIVATE
		DLLInterface.cpp
		DLLInterface.h
		KeyMap.cpp
		KeyMap.h
		actored.h
		precompiled.cpp
		precompiled.h
		$<$<PLATFORM_ID:Windows>: atlas.rc> # Caution, the blank before the File is needed
)

add_subdirectory(Graphics/)
