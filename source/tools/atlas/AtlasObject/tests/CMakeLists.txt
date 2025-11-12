cxxtest_add_test(test_atlas_object_xml testAtlasObjectXML.cpp ${CMAKE_CURRENT_LIST_DIR}/test_AtlasObjectXML.h)

target_include_directories(test_atlas_object_xml
	PRIVATE
		${CMAKE_SOURCE_DIR}/source
		${CXXTEST_INCLUDE_DIRS}
)

target_link_libraries(test_atlas_object_xml
	PRIVATE
		${BUILD_FLAGS_TARGET}
		AtlasObject
)
