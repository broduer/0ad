function(find_prebuild_library _name)
	include(FindPackageHandleStandardArgs)
	set(multi_args COMPONENTS PATHS)
	set(single_args INC_PATH VERSION)
	cmake_parse_arguments(args "${multi_args}" "${single_args}" "" ${ARGN})

	string(TOLOWER ${_name} libname)
	string(TOUPPER ${_name} target_name)
	# ++++++++ Handle COMPONENTS +++++++++++++++++++++
	if(args_COMPONENTS)

	endif()

	# ++++++++ Find relevant elements ++++++++++++++++
	find_library(Lib${_name} NAMES ${libname} ${libname}${args_VERSION})
	if(NOT args_INC_PATH)
		find_path(Lib${_name}_inc NAMES ${libname}.h PATHS ${0AD_EXT_LIBDIR}/${_name}/include/)
		# recursive add all paths beneth included...
	else()
		set(Lib${_name}_inc ${args_INC_PATH})
	endif()
	if(NOT Lib${_name}_inc)
		set(Lib${_name}_inc ${0AD_EXT_LIBDIR}/${_name}/include/)
	endif()

	find_package_handle_standard_args(Lib${_name} REQUIRED_VARS Lib${_name})
	message(STATUS "${Lib${_name}} - ${Lib${_name}_inc} - ${Lib${_name}_FOUND} - ${target_name}::${target_name}")

	if (Lib${_name}_FOUND)
		mark_as_advanced(
			Lib${_name}
			Lib${_name}_inc
		)
	endif()
	if(Lib${_name} MATCHES "LibBoost")
		add_library(${target_name}::headers INTERFACE IMPORTED)
		set_target_properties(${target_name}::headers PROPERTIES
			IMPORTED_LOCATION ${Lib${_name}_inc}
			INTERFACE_INCLUDE_DIRECTORIES ${Lib${_name}_inc}
		)
	elseif(Lib${_name} MATCHES "SDL2")
		add_library(${target_name}::${target_name} UNKNOWN IMPORTED)
		set_target_properties(${target_name}::${target_name} PROPERTIES
			IMPORTED_LOCATION ${Lib${_name}}
			INTERFACE_INCLUDE_DIRECTORIES ${Lib${_name}_inc}/SDL
		)
	elseif (Lib${_name} AND NOT TARGET ${target_name}::${target_name})
		add_library(${target_name}::${target_name} UNKNOWN IMPORTED)
		set_target_properties(${target_name}::${target_name} PROPERTIES
			IMPORTED_LOCATION ${Lib${_name}}
			INTERFACE_INCLUDE_DIRECTORIES ${Lib${_name}_inc}
		)
	endif()
endfunction()
