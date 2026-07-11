# ulmk_apps — external components consumed by the ulmk kernel build.
cmake_minimum_required(VERSION 3.20)

file(GLOB _dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(_dir ${_dirs})
	if(EXISTS "${_dir}/CMakeLists.txt")
		get_filename_component(_name "${_dir}" NAME)
		add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/_apps/${_name}")
	endif()
endforeach()
