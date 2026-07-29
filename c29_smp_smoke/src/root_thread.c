/* SPDX-License-Identifier: MIT */
/*
 * c29_smp_smoke — bring up CPU2/CPU3, wait ready mask 0x7, ping IPIs.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/config.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_puts(const char *s);

uint32_t ulmk_arch_smp_ready_mask(void);
void ulmk_arch_send_ipi(uint32_t cpu_id);

#ifndef ULMK_C29_SMP_CPUS
#define ULMK_C29_SMP_CPUS	3u
#endif

#if ULMK_CONFIG_ENABLE_SMP
static volatile uint32_t g_cpu_hits[ULMK_C29_SMP_CPUS];

static void worker(void *arg)
{
	uint32_t cpu = (uint32_t)(uintptr_t)arg;

	for (;;) {
		if (cpu < ULMK_C29_SMP_CPUS)
			g_cpu_hits[cpu]++;
		(void)ulmk_sleep_ms(50u);
	}
}
#endif

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
#if ULMK_CONFIG_ENABLE_SMP
	ulmk_thread_attr_t attr;
	uint32_t mask;
	uint32_t i;
	uint32_t target;

	board_services_init(info);
	board_console_puts("ULMK-HIL:c29_smp_smoke:START\n");

	mask = ulmk_arch_smp_ready_mask();
	board_console_puts("ulmk: c29_smp_smoke ready check\n");

	if (mask != 0x7u) {
		board_console_puts("ULMK-HIL:c29_smp_smoke:FAIL\n");
		board_console_puts("C29SMP_FAIL\n");
		for (;;)
			(void)ulmk_sleep_ms(1000u);
	}

	board_console_puts("ULMK-HIL:c29_smp_smoke:CORE_READY mask=0x7\n");

	for (i = 1u; i < ULMK_C29_SMP_CPUS; i++) {
		attr = (ulmk_thread_attr_t){
			.name = "smp_w",
			.entry = worker,
			.arg = (void *)(uintptr_t)i,
			.priority = 2u,
			.stack_size = 2048u,
			.privilege = ULMK_PRIV_USER,
			.cpu = (uint8_t)i,
		};
		(void)ulmk_thread_create(&attr);
	}

	for (i = 0u; i < ULMK_C29_SMP_CPUS; i++) {
		for (target = 0u; target < ULMK_C29_SMP_CPUS; target++) {
			if (i == target)
				continue;
			ulmk_arch_send_ipi(target);
		}
	}

	(void)ulmk_sleep_ms(500u);
	board_console_puts("ULMK-HIL:c29_smp_smoke:PASS\n");
	board_console_puts("C29SMP_PASS\n");

	for (;;)
		(void)ulmk_sleep_ms(1000u);
#else
	board_services_init(info);
	board_console_puts("ulmk: c29_smp_smoke needs --enable-smp\n");
	board_console_puts("ULMK-HIL:c29_smp_smoke:SKIP\n");
	for (;;)
		(void)ulmk_sleep_ms(1000u);
#endif
}
