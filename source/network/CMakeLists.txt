add_library(network STATIC)

target_sources(network
	PRIVATE
		FSM.h
		HttpServer.cpp
		HttpServer.h
		NMTCreator.h
		NetClient.cpp
		NetClient.h
		NetClientSession.cpp
		NetClientSession.h
		NetClientTurnManager.cpp
		NetClientTurnManager.h
		NetEnet.cpp
		NetEnet.h
		NetFileTransfer.cpp
		NetFileTransfer.h
		NetHost.cpp
		NetHost.h
		NetMessage.cpp
		NetMessage.h
		NetMessageSim.cpp
		NetMessages.h
		NetProtocol.cpp
		NetProtocol.h
		NetServer.cpp
		NetServer.h
		NetServerSession.cpp
		NetServerSession.h
		NetServerTurnManager.cpp
		NetServerTurnManager.h
		NetStats.cpp
		NetStats.h
		Serialization.h
		StringConverters.h
		StunClient.cpp
		StunClient.h
)

target_include_directories(network
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)

target_link_libraries(network
	PRIVATE
		${BUILD_FLAGS_TARGET}
		ENet::ENet
		SDL2::SDL2
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		fmt::fmt
		LibXml2::LibXml2
		Iconv::Iconv
		$<$<NOT:$<BOOL:${without-miniupnpc}>>:Miniupnpc::Miniupnpc>
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		$<IF:$<OR:$<BOOL:${with-system-cpp-httplib}>,$<PLATFORM_ID:Windows>>,CppHTTPlib::CppHTTPlib,httplib>
)

disable_rtti(network)

if(NOT without-pch)
	add_pch(TARGET network PCH_DIR ${PROJECT_SOURCE_DIR}/pch/network)
else()
	target_compile_definitions(network PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(network
		PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
	target_include_directories(network
		BEFORE PRIVATE
			${PROJECT_SOURCE_DIR}/pch/network
	)
endif()

add_subdirectory(scripting/)
if(NOT without-tests)
	add_subdirectory(tests/)
endif()
