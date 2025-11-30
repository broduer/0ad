target_sources(lowlevel
	PRIVATE
		vfs.cpp
		vfs.h
		vfs_lookup.cpp
		vfs_lookup.h
		vfs_path.h
		vfs_populate.cpp
		vfs_populate.h
		vfs_tree.cpp
		vfs_tree.h
		vfs_util.cpp
		vfs_util.h
)

if(NOT without-tests)
	add_subdirectory(tests/)
endif()
