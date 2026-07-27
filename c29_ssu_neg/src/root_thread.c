/* SPDX-License-Identifier: MIT */
/*
 * c29_ssu_neg — negative isolation against PIPE MMIO.
 *
 * SSUMODE2 + enforcing APR: write must kill this thread (no PASS alive).
 * SSUMODE1 bring-up: SKIP with explicit HIL marker.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);

bool ulmk_arch_ssu_is_enforcing(void);
uint32_t ulmk_arch_ssu_mode(void);

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	volatile uint32_t *pipe_ctl;
	uint32_t mode;

	board_services_init(info);
	mode = ulmk_arch_ssu_mode();

	board_console_puts("ULMK-HIL:c29_ssu_neg:START\n");
	board_console_puts("ulmk: c29_ssu_neg — probing PIPE MMIO\n");
	if (mode == 0x0Cu)
		board_console_puts("ulmk: SSU mode=0x0c (MODE2)\n");
	else if (mode == 0x30u)
		board_console_puts("ulmk: SSU mode=0x30 (MODE1)\n");
	else
		board_console_puts("ulmk: SSU mode=other\n");

	if (!ulmk_arch_ssu_is_enforcing()) {
		board_console_puts("ulmk: SSU not enforcing (MODE1 bring-up)\n");
		board_console_puts("ULMK-HIL:c29_ssu_neg:SKIP\n");
		board_console_puts("C29SSU_SKIP\n");
		for (;;)
			(void)ulmk_sleep_ms(1000u);
	}

	board_console_puts("ulmk: SSU enforcing — expect fault kill\n");

	/*
	 * Under enforcing SSU this write must fault and kill the thread
	 * without returning.  A following FAIL line means Gate D failed.
	 * HIL also accepts "TRAP: killing" from ulmk_kern_trap_recoverable.
	 */
	pipe_ctl = (volatile uint32_t *)(uintptr_t)0x30022000u;
	*pipe_ctl = 1u;

	board_console_puts("ULMK-HIL:c29_ssu_neg:FAIL\n");
	board_console_puts("C29SSU_FAIL\n");
	for (;;)
		(void)ulmk_sleep_ms(1000u);
}
