/* SPDX-License-Identifier: MIT */
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_display.h>
#include <ulmk_device_input.h>
#include "board_console.h"
#include "board_services.h"
#include "board_devices.h"
#include "board_config.h"
#include "app_benchmark.h"

void app_benchmark_run(ulmk_dev_t *disp, ulmk_dev_t *input);

/*
 * Kernel root stack is small.  Scrolling / opa_layered SW blend need a deep
 * stack — run the demo on a dedicated DRIVER thread.
 *
 * Devices come from board_devices_register() + ulmk_open pathname ABI so the
 * same app binary works on any board that exports /dev/disp0 and /dev/input0.
 */
#define LVGL_BENCH_STACK	(128u * 1024u)

static ulmk_dev_t g_disp;
static ulmk_dev_t g_input;

static void lvgl_bench_thread(void *arg)
{
	(void)arg;
	board_console_puts("lvgl_bench running\r\n");
	/*
	 * FB + LVGL heap sit in board SDRAM/PSRAM.  Map into *this* thread —
	 * board_devices_register()'s map on root does not cover workers.
	 */
	if (board_devices_map_fb() != ULMK_OK) {
		board_console_puts("board_devices_map_fb failed\r\n");
		ulmk_thread_exit();
	}
	app_benchmark_run(&g_disp, &g_input);
	ulmk_thread_exit();
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;

	board_services_init(info);
	board_console_puts("\r\nlvgl benchmark boot\r\n");

	if (board_devices_register() != ULMK_OK) {
		board_console_puts("board_devices_register failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_open("/dev/disp0", &g_disp) != ULMK_OK) {
		board_console_puts("open /dev/disp0 failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_disp_on(&g_disp, 1) != ULMK_OK) {
		board_console_puts("disp on failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_open("/dev/input0", &g_input) != ULMK_OK) {
		board_console_puts("open /dev/input0 failed\r\n");
		ulmk_thread_exit();
	}

	attr = (ulmk_thread_attr_t){
		.name = "lvgl_bench",
		.entry = lvgl_bench_thread,
		.arg = NULL,
		.priority = 10u,
		.stack_size = LVGL_BENCH_STACK,
		.privilege = ULMK_PRIV_DRIVER,
		.heap_size = 0u,
		.cpu = 0u,
	};
	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID) {
		board_console_puts("lvgl_bench spawn failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_cap_grant(tid, ULMK_CAP_MAP_SHARED | ULMK_CAP_MAP_PERIPH |
				 ULMK_CAP_IRQ) != ULMK_OK) {
		board_console_puts("lvgl_bench cap grant failed\r\n");
		ulmk_thread_exit();
	}
#if defined(ULMK_BOARD_SDRAM_BASE)
	/*
	 * Prefer grant of the root SDRAM window so the worker can paint even
	 * if it races ahead of its own map_fb (maps are per-thread).
	 */
	if (ulmk_mem_grant((void *)(uintptr_t)ULMK_BOARD_SDRAM_BASE,
			   ULMK_BOARD_SDRAM_SIZE, tid,
			   ULMK_PERM_READ | ULMK_PERM_WRITE) != ULMK_OK) {
		board_console_puts("lvgl_bench sdram grant failed\r\n");
		ulmk_thread_exit();
	}
#endif
	board_console_puts("lvgl_bench spawned\r\n");
	ulmk_thread_exit();
}
