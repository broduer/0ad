cmake_minimum_required(VERSION 3.25.1...4.0.0)

# 0AD Specific Functions

# Add Precompiled Headers. If no target is given, this macro will fail with a FATAL_ERROR.
# rationale: we need one PCH per static lib, since one global header would increase dependencies. To that end, we can either include them as
# "projectdir/precompiled.h", or add "source/PCH/projectdir" to the include path and put the PCH there. The latter is better because many
# projects contain several dirs and it's unclear where there the PCH should be stored. This way is also a bit easier to use in that
# source files always include "precompiled.h".
# Notes:
# * Visual Assist manages to use the project include path and can correctly open these files from the IDE.
# * precompiled.cpp (needed to "Create" the PCH) also goes in the abovementioned dir.
# * using CMakes precompiled Header is not possible, as it does not allow for a custom name like used here.
function(add_pch)
	set(single_args TARGET PCH_DIR)
	cmake_parse_arguments(args "" "${single_args}" "" ${ARGN})
	if(NOT args_TARGET)
		message(FATAL_ERROR "add_pch: Missing target!!")
	endif()

	include(PrecompiledHeader)
	if(NOT args_PCH_DIR)
		get_target_property(source_root ${args_TARGET} SOURCE_DIR)
		set(args_PCH_DIR ${source_root}/pch/${args_TARGET})
	endif()
	# Put the project-specific PCH directory at the start of the include path, so '#include "precompiled.h"' will look in there first
	target_include_directories(${args_TARGET}
		BEFORE PRIVATE
			${args_PCH_DIR}/
	)
	if (CMAKE_GENERATOR MATCHES "Visual Studio")
		set(pch_header "precompiled.h")
	elseif(CMAKE_GENERATOR MATCHES "Xcode")
		set(pch_header "../${args_PCH_DIR}/precompiled.h")
	else()
		set(pch_header "${args_PCH_DIR}/precompiled.h")
	endif()

	precompile_header(${args_TARGET} ${pch_header} ${args_PCH_DIR}/precompiled.cpp)

	target_sources(${args_TARGET}
		PRIVATE
			${args_PCH_DIR}/precompiled.cpp
			${args_PCH_DIR}/precompiled.h
	)
	target_compile_definitions(${args_TARGET} PRIVATE CONFIG_ENABLE_PCH=1)
endfunction(add_pch)
