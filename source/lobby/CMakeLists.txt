add_library(lobby STATIC)

if(NOT without-pch)
	add_pch(TARGET lobby PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/lobby)
else()
	target_compile_definitions(lobby PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(lobby PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

target_sources(lobby
	PRIVATE
		Globals.cpp
		GlooxConversion.cpp
		GlooxConversion.h
		StanzaExtensions.cpp
		StanzaExtensions.h
		XmppClient.cpp
		XmppClient.h
		# Add i18n files
		$<$<NOT:$<BOOL:${without-lobby}>>:${CMAKE_SOURCE_DIR}/source/i18n/L10n.cpp>
		$<$<NOT:$<BOOL:${without-lobby}>>:${CMAKE_SOURCE_DIR}/source/i18n/L10n.h>
)

target_link_libraries(lobby
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		$<$<NOT:$<BOOL:${without-lobby}>>:ENet::ENet>
		$<$<NOT:$<BOOL:${without-lobby}>>:Gloox::Gloox>
		$<$<NOT:$<BOOL:${without-lobby}>>:ICU::i18n>
		$<$<NOT:$<BOOL:${without-lobby}>>:ICU::uc>
		$<$<NOT:$<BOOL:${without-lobby}>>:Iconv::Iconv>
		Sodium::Sodium
		$<$<NOT:$<BOOL:${without-lobby}>>:tinygettext>
		fmt::fmt
)

target_include_directories(lobby
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)

disable_rtti(lobby)

add_subdirectory(scripting/)
