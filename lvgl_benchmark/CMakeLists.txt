# LVGL benchmark — board-agnostic via ulmk_device_manager display/input.
set(_LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../deps/lvgl")

set(_LVGL_BENCH_SRCS
	src/root_thread.c
	src/app_benchmark.c
	src/port_disp.c
	src/port_indev.c
	src/port_tick.c
)
# PSRAM scanout stress is ESP32-P4 only.
if("${ULMK_CHIP_DIR}" MATCHES "esp32p4")
	list(APPEND _LVGL_BENCH_SRCS src/psram_stress.c)
endif()

ulmk_component_register(
	NAME lvgl_benchmark
	ENABLED OFF
	SOURCES ${_LVGL_BENCH_SRCS}
	REQUIRES
		ulmk_device_manager
		ulmk_device_classes
	ROOT_THREAD
)

if(NOT TARGET ulmk_comp_lvgl_benchmark)
	return()
endif()

if(NOT EXISTS "${_LVGL_DIR}/lvgl.h")
	message(FATAL_ERROR "LVGL submodule missing at ${_LVGL_DIR} (tag v9.5.0)")
endif()

file(GLOB_RECURSE _LVGL_SRC CONFIGURE_DEPENDS "${_LVGL_DIR}/src/*.c")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/thorvg/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/ffmpeg/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/libjpeg_turbo/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/libpng/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/lodepng/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/gif/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/qrcode/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/barcode/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/tiny_ttf/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/libs/freetype/.*")
list(FILTER _LVGL_SRC EXCLUDE REGEX ".*/drivers/.*")

file(GLOB _LVGL_DEMO_SRC CONFIGURE_DEPENDS
	"${_LVGL_DIR}/demos/lv_demos.c"
	"${_LVGL_DIR}/demos/benchmark/*.c"
	"${_LVGL_DIR}/demos/benchmark/assets/*.c"
	"${_LVGL_DIR}/demos/widgets/*.c"
	"${_LVGL_DIR}/demos/widgets/assets/*.c"
)
# BSP overlay: opa card cap + scene logs (keep upstream submodule clean).
list(FILTER _LVGL_DEMO_SRC EXCLUDE REGEX ".*/demos/benchmark/lv_demo_benchmark\\.c$")
list(APPEND _LVGL_DEMO_SRC
	"${CMAKE_CURRENT_LIST_DIR}/demos/lv_demo_benchmark.c")

add_library(ulmk_lvgl STATIC ${_LVGL_SRC})
add_library(ulmk_lvgl_demos STATIC ${_LVGL_DEMO_SRC})

foreach(_tgt ulmk_lvgl ulmk_lvgl_demos)
	target_include_directories(${_tgt} PUBLIC
		"${_LVGL_DIR}"
		"${_LVGL_DIR}/demos/benchmark"
		"${CMAKE_CURRENT_LIST_DIR}"
		"${CMAKE_CURRENT_LIST_DIR}/src"
		"${ULMK_CHIP_DIR}"
	)
	target_compile_definitions(${_tgt} PUBLIC
		LV_CONF_INCLUDE_SIMPLE
		LV_LVGL_H_INCLUDE_SIMPLE
	)
	if(DEFINED ULMK_BOARD_CFLAGS)
		set(_lvgl_cflags "${ULMK_BOARD_CFLAGS}")
		list(REMOVE_ITEM _lvgl_cflags "-DESP_PLATFORM")
		target_compile_options(${_tgt} PRIVATE ${_lvgl_cflags})
	endif()
	target_compile_options(${_tgt} PRIVATE
		-Ofast
		-UESP_PLATFORM
		-Wno-unused-parameter
		-Wno-empty-body
		-Wno-missing-field-initializers
	)
endforeach()

target_link_libraries(ulmk_lvgl_demos PUBLIC ulmk_lvgl)

target_include_directories(ulmk_comp_lvgl_benchmark PRIVATE
	"${CMAKE_CURRENT_LIST_DIR}/src"
	"${_LVGL_DIR}"
	"${_LVGL_DIR}/demos"
	"${CMAKE_CURRENT_LIST_DIR}"
	"${CMAKE_SOURCE_DIR}/components/ulmk_device_manager/include"
)
target_compile_definitions(ulmk_comp_lvgl_benchmark PRIVATE
	LV_CONF_INCLUDE_SIMPLE
	LV_LVGL_H_INCLUDE_SIMPLE
)
target_compile_options(ulmk_comp_lvgl_benchmark PRIVATE
	-Ofast
	-UESP_PLATFORM
)
target_link_libraries(ulmk_comp_lvgl_benchmark PUBLIC
	ulmk_lvgl
	ulmk_lvgl_demos
)
