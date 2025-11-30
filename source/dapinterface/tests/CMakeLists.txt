# cxxtest_add_test(test_dapinterface test_DapInterface.cpp ${CMAKE_CURRENT_LIST_DIR}/test_DapInterface.h)

# disable_rtti(test_dapinterface)

# target_include_directories(test_dapinterface
# 	PRIVATE
# 		${CMAKE_SOURCE_DIR}/source
# 		${CXXTEST_INCLUDE_DIRS}

# )

# target_link_libraries(test_dapinterface
# 	PRIVATE
# 		${BUILD_FLAGS_TARGET}
# 		$<$<PLATFORM_ID:Linux,Darwin,FreeBSD>:Boost::headers>
# 		$<$<PLATFORM_ID:Windows>:BOOST::headers>
# 		fmt::fmt
# 		$<IF:$<BOOL:${with-system-mozjs}>,Mozjs::Mozjs,BdlMozjs::BdlMozjs>
# 		dapinterface
# )
