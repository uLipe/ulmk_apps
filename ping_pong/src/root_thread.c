/* SPDX-License-Identifier: MIT */
/*
 * Root thread — ulmk_apps/ping_pong standalone IPC demo.
 */

#include <ulmk/microkernel.h>
#include <ping_pong.h>

void board_services_init(const ulmk_boot_info_t *info);

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	board_services_init(info);
	ping_pong_init(info);
	ulmk_thread_exit();
}
