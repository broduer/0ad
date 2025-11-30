target_sources(engine
	PRIVATE
		Atlas.cpp
		Atlas.h
		CmdLineArgs.cpp
		CmdLineArgs.h
		Config.cpp
		Config.h
		GameSetup.cpp
		GameSetup.h
		HWDetect.cpp
		HWDetect.h
		Paths.cpp
		Paths.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
