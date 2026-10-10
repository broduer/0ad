if(CMAKE_SYSTEM_NAME MATCHES "Linux")
	include(GNUInstallDirs)
	install(
		FILES
			${CMAKE_CURRENT_SOURCE_DIR}/ActorEditor.ico
			${CMAKE_CURRENT_SOURCE_DIR}/ArchiveViewer.ico
			${CMAKE_CURRENT_SOURCE_DIR}/FileConverter.ico
		DESTINATION
			${CMAKE_INSTALL_DATADIR}/0ad/tools/ActorEditor/icons
		COMPONENT resources
	)
	install(
		FILES
			${CMAKE_CURRENT_SOURCE_DIR}/ScenarioEditor.ico
		DESTINATION
			${CMAKE_INSTALL_DATADIR}/0ad/tools/atlas/icons
		COMPONENT resources
	)
endif()
