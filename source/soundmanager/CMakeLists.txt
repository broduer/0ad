target_sources(engine
	PRIVATE
		ISoundManager.h
		SoundManager.cpp
		SoundManager.h
)

add_subdirectory(data/)
add_subdirectory(items/)
add_subdirectory(scripting/)
