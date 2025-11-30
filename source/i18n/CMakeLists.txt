target_sources(engine
	PRIVATE
		L10n.cpp
		L10n.h
)

target_sources(gui
	PRIVATE
		L10n.cpp
		L10n.h
)

add_subdirectory(scripting/)
