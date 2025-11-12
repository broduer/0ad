# Adds a custom precompiled header. This header must be included in the code if not added with FORCE_INCLUDE
function(precompile_header _target _header _source)
	set(single_args FORCE_INCLUDE)
	cmake_parse_arguments(args "" "${single_args}" "" ${ARGN})

	if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_C_COMPILER_ID STREQUAL "MSVC")
		# Solution based opon https://www.dominikgrabiec.com/posts/2022/10/18/precompiled_header_snippet.html
		target_compile_options(${_target}
			PRIVATE
				"/Yu${_header}"
		)
		set_source_files_properties(${_source}
			PROPERTIES
				COMPILE_OPTIONS	"/Yc${_header}"
		)
		if(${args_FORCE_INCLUDE})
			set_source_files_properties(${_source}
				PROPERTIES
					COMPILE_OPTIONS	/FI${_header}
			)
		endif()
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang" OR CMAKE_C_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
		# Solution is based on internal cmake code
		set_source_files_properties(${_source}
			PROPERTIES
				COMPILE_OPTIONS		-Winvalid-pch -x ${_header}.gch
		)
		if(${args_FORCE_INCLUDE})
			set_source_files_properties(${_source}
				PROPERTIES
					COMPILE_OPTIONS	include ${_header}.gch
			)
		endif()
	endif()
endfunction()
