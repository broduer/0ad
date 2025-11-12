target_sources(AtlasUI
	PRIVATE
		ScenarioEditor.cpp
		ScenarioEditor.h
		SectionLayout.cpp
		SectionLayout.h
)

add_subdirectory(Sections/)
add_subdirectory(Tools/)
