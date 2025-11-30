add_library(gui STATIC)

target_sources(gui
	PRIVATE
		CGUI.cpp
		CGUI.h
		CGUIScrollBarHorizontal.cpp
		CGUIScrollBarHorizontal.h
		CGUIScrollBarVertical.cpp
		CGUIScrollBarVertical.h
		CGUISetting.cpp
		CGUISetting.h
		CGUISprite.cpp
		CGUISprite.h
		CGUIText.cpp
		CGUIText.h
		GUIManager.cpp
		GUIManager.h
		GUIObjectEventBroadcaster.h
		GUIObjectTypes.cpp
		GUIRenderer.cpp
		GUIRenderer.h
		GUIStringConversions.cpp
		GUITooltip.cpp
		GUITooltip.h
		IGUIScrollBar.cpp
		IGUIScrollBar.h
		SGUIIcon.h
		SGUIMessage.h
		SGUIStyle.h
)
target_include_directories(gui
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)
target_link_libraries(gui
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		SDL2::SDL2
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		ENet::ENet
		tinygettext
		ICU::i18n
		ICU::uc
		Iconv::Iconv
		fmt::fmt
		LibXml2::LibXml2
		$<$<NOT:$<BOOL:${without-audio}>>:OpenAL::OpenAL>
)

disable_rtti(gui)

if(NOT without-pch)
	add_pch(TARGET gui PCH_DIR ${CMAKE_SOURCE_DIR}/source/pch/gui)
else()
	target_compile_definitions(gui PRIVATE CONFIG_ENABLE_PCH=0)
	set_target_properties(gui PROPERTIES
		DISABLE_PRECOMPILE_HEADERS TRUE
	)
endif()

add_subdirectory(ObjectBases/)
add_subdirectory(ObjectTypes/)
add_subdirectory(Scripting/)
add_subdirectory(SettingTypes/)
if(NOT without-tests)
	add_subdirectory(tests/)
endif()
