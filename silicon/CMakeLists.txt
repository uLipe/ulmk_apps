# ulmk_apps/silicon — aggregator for silicon_* cert components.
cmake_minimum_required(VERSION 3.20)

file(GLOB _dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(_dir IN LISTS _dirs)
	if(IS_DIRECTORY "${_dir}" AND EXISTS "${_dir}/CMakeLists.txt")
		get_filename_component(_name "${_dir}" NAME)
		add_subdirectory("${_dir}"
			"${CMAKE_BINARY_DIR}/ulmk_apps/silicon/${_name}")
	endif()
endforeach()
