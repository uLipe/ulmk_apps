/* SPDX-License-Identifier: MIT */
/*
 * silicon_baseline — minimal TC275 HIL root thread (console + hello only).
 *
 * Skips board_timer and ping_pong so silicon bring-up can validate:
 *   ulmk_root_thread → board_console_start → hello IPC print
 */

#include <stdint.h>
#include <ulmk/microkernel.h>

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);

static void hello_entry(void *arg)
{
	(void)arg;

	ulmk_board_hil_mark(5u);
	board_console_puts("ulmk: hello from userspace - hello world!\n");
	ulmk_thread_exit();
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t         tid;

	ulmk_board_hil_mark(1u);
	board_console_start(info);
	ulmk_board_hil_mark(2u);

	attr.name       = "hello";
	attr.entry      = hello_entry;
	attr.arg        = NULL;
	attr.priority   = 10u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_USER;

	tid = ulmk_thread_create(&attr);
	(void)tid;

	ulmk_thread_exit();
}
