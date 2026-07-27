/* SPDX-License-Identifier: MIT */
/*
 * C29 RAM bring-up banner — sleep heartbeat proves CPUTIMER2 wake after
 * deferred RETI.INT to idle (Gate B).
 */

#include <ulmk/microkernel.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	unsigned n;

	board_services_init(info);

	board_console_puts("ulmk: c29_banner on LAUNCHXL-F29H85X\n");
	board_console_puts("ulmk: root thread running\n");
	board_console_puts("ULMK-HIL:c29_sleep:START\n");

	for (n = 0u; n < 4u; n++) {
		(void)ulmk_sleep_ms(500u);
		board_console_puts("ulmk: c29_banner alive\n");
	}

	/*
	 * Dense marker: polled UART can drop bytes under tick IRQ load,
	 * so keep a short unique sentinel for HIL grep.
	 */
	board_console_puts("ULMK-HIL:c29_sleep:PASS\n");
	board_console_puts("C29SLEEP_PASS\n");

	for (;;) {
		board_console_puts("ulmk: c29_banner alive\n");
		(void)ulmk_sleep_ms(500u);
	}
}
