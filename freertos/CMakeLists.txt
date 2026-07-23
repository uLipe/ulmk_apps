# SPDX-License-Identifier: MIT
# FreeRTOS-compatible userspace API on top of ulmk (not a second scheduler).

ulmk_component_register(
	NAME         freertos
	ENABLED      OFF
	SOURCES
		src/freertos_init.c
		src/freertos_task.c
		src/freertos_semphr.c
		src/freertos_queue.c
		src/freertos_timers.c
	INCLUDE_DIRS include
)
