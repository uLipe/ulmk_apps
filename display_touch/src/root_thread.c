/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk_device.h>
#include <ulmk_device_input.h>
#include "board_console.h"
#include "board_services.h"
#include "board_devices.h"
#include "board_timer.h"

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_dev_t indev;
	struct ulmk_input_event ev;
	int n;
	int last = -1;

	board_services_init(info);
	board_console_puts("display_touch (ulmk_apps)\r\n");

	if (board_devices_register() != ULMK_OK) {
		board_console_puts("board_devices_register failed\r\n");
		ulmk_thread_exit();
	}
	if (ulmk_open("/dev/input0", &indev) != ULMK_OK) {
		board_console_puts("open /dev/input0 failed\r\n");
		ulmk_thread_exit();
	}
	board_console_puts("display_touch: ready\r\n");

	for (;;) {
		n = ulmk_read(&indev, &ev, sizeof(ev));
		if (n == (int)sizeof(ev) && (int)ev.state != last) {
			last = (int)ev.state;
			board_console_puts(ev.state ? "press\r\n" : "release\r\n");
		}
		board_timer_sleep_us(20000u);
	}
}
