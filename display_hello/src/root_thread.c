/* SPDX-License-Identifier: MIT */
/*
 * display_hello — board-agnostic smoke via ulmk_device_manager display class.
 */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_display.h>
#include "board_console.h"
#include "board_services.h"
#include "board_devices.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_dev_t disp;
	void *fb;
	uint16_t *pix;
	uint32_t i;
	uint32_t w;
	uint32_t h;
	uint32_t stride;
	uint32_t n;

	board_services_init(info);
	board_console_puts("display_hello (ulmk_apps)\r\n");

	if (board_devices_register() != ULMK_OK) {
		board_console_puts("board_devices_register failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_open("/dev/disp0", &disp) != ULMK_OK) {
		board_console_puts("open /dev/disp0 failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_disp_on(&disp, 1) != ULMK_OK) {
		board_console_puts("disp on failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_disp_info(&disp, &w, &h, NULL, &stride, NULL) != ULMK_OK) {
		board_console_puts("disp info failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_disp_get_fb(&disp, 0u, &fb) != ULMK_OK || !fb) {
		board_console_puts("disp get_fb failed\r\n");
		ulmk_thread_exit();
	}

	pix = (uint16_t *)fb;
	n = w * h;
	for (i = 0u; i < n; i++)
		pix[i] = 0x001Fu; /* blue-ish */
	if (ulmk_disp_write_present(&disp, fb, NULL, 0u) != ULMK_OK) {
		board_console_puts("disp present failed\r\n");
		ulmk_thread_exit();
	}

	board_console_puts("display_hello: PASS\r\n");
	for (;;)
		board_timer_sleep_us(1000000u);
}
