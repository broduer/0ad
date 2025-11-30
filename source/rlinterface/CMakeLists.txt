add_library(rlinterface STATIC)

target_link_libraries(rlinterface
	PRIVATE
		${BUILD_FLAGS_TARGET}
		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
		$<$<PLATFORM_ID:Windows>:BOOST::headers>
		fmt::fmt
		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
		$<IF:$<OR:$<BOOL:${with-system-cpp-httplib}>,$<PLATFORM_ID:Windows>>,CppHTTPlib::CppHTTPlib,httplib>
)

target_sources(rlinterface
	PRIVATE
		RLInterface.cpp
		RLInterface.h
)

target_include_directories(rlinterface
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
)

disable_rtti(rlinterface)
