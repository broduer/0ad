target_sources(lowlevel
	PRIVATE
		$<IF:$<CXX_COMPILER_ID:MSVC>,${CMAKE_CURRENT_SOURCE_DIR}/msc/msc.cpp,${CMAKE_CURRENT_SOURCE_DIR}/gcc/gcc.cpp>
)
