target_sources(AtlasObject
	PRIVATE
		AtlasObject.h
		AtlasObjectImpl.cpp
		AtlasObjectImpl.h
		AtlasObjectJS.cpp
		AtlasObjectText.cpp
		AtlasObjectText.h
		AtlasObjectXML.cpp
		JSONSpiritInclude.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
