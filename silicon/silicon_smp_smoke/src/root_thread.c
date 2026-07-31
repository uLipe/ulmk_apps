/* SPDX-License-Identifier: MIT */
/*
 * silicon_smp_smoke — HIL: every secondary alive + affinity pin + IPI wake.
 *
 * Sweeps every secondary the board declares rather than just the first one:
 * each core has its own interrupt routing, so CPU1 running says nothing
 * about CPU2.  dev.py forces ULMK_CONFIG_ENABLE_SMP for this case.
 *
 * Console via board_console (UART + optional RAM log) so serial HIL works
 * on boards without OpenOCD RAM dump.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk/platform.h>

#ifndef ULMK_ARCH_NUM_CPU
#define ULMK_ARCH_NUM_CPU	1
#endif

void ulmk_board_hil_mark(uint32_t n);

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);

void __attribute__((noinline)) silicon_smp_smoke_done(void);

/*
 * One slot per core, holding cpu_id + 1 so that 0 stays the "has not run yet"
 * state the poll waits on.  Storing the id rather than a flag catches a
 * worker that runs on the wrong core instead of just counting it as alive.
 */
static ULMK_PRIVATE volatile uint32_t g_seen[ULMK_ARCH_NUM_CPU];

static void puts_u32(uint32_t v)
{
	char buf[12];
	uint32_t n = 0u;

	do {
		buf[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while (v != 0u);

	while (n--)
		board_console_putc(buf[n]);
}

static void worker(void *arg)
{
	uint32_t pinned_to = (uint32_t)(uintptr_t)arg;

	g_seen[pinned_to] = ulmk_cpu_id() + 1u;
	ulmk_thread_exit();
}

void __attribute__((noinline)) silicon_smp_smoke_done(void)
{
	__asm__ volatile("" ::: "memory");
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_thread_attr_t attr = {0};
	ulmk_tid_t tid;
	uint32_t cpu;
	uint32_t i;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_SMP_SMOKE: begin\n");
	ulmk_board_hil_mark(6u);

	if (ulmk_cpu_id() != 0u) {
		board_console_puts("SILICON_SMP_SMOKE: FAIL root cpu\n");
		ulmk_board_hil_mark(0xDEADu);
		silicon_smp_smoke_done();
		ulmk_thread_exit();
	}

	board_console_puts("SILICON_SMP_SMOKE: cpus=");
	puts_u32((uint32_t)ULMK_ARCH_NUM_CPU);
	board_console_putc('\n');

	for (cpu = 1u; cpu < (uint32_t)ULMK_ARCH_NUM_CPU; cpu++) {
		attr.name       = "smpw";
		attr.entry      = worker;
		attr.arg        = (void *)(uintptr_t)cpu;
		attr.priority   = 1u;
		attr.stack_size = 2048u;
		attr.privilege  = ULMK_PRIV_DRIVER;
		attr.heap_size  = 0u;
		attr.cpu        = (uint8_t)cpu;
		ulmk_board_hil_mark(0xC100u | cpu);

		tid = ulmk_thread_create(&attr);
		if (tid == ULMK_TID_INVALID || (int32_t)tid < 0) {
			ulmk_board_hil_mark(0xDEAD0000u | cpu);
			board_console_puts("SILICON_SMP_SMOKE: FAIL spawn cpu");
			puts_u32(cpu);
			board_console_putc('\n');
			silicon_smp_smoke_done();
			ulmk_thread_exit();
		}
	}
	ulmk_board_hil_mark(0xC200u);

	/*
	 * Spawn everyone first, then collect: a core that only wakes because
	 * the previous one was already parked would still pass a serialised
	 * spawn-and-wait.
	 */
	for (cpu = 1u; cpu < (uint32_t)ULMK_ARCH_NUM_CPU; cpu++) {
		for (i = 0u; i < 400000u && g_seen[cpu] == 0u; i++)
			ulmk_thread_yield();

		if (g_seen[cpu] == 0u) {
			ulmk_board_hil_mark(0xDEAD0000u | cpu);
			board_console_puts("SILICON_SMP_SMOKE: FAIL cpu");
			puts_u32(cpu);
			board_console_puts(" not seen\n");
			silicon_smp_smoke_done();
			ulmk_thread_exit();
		}
		if (g_seen[cpu] - 1u != cpu) {
			ulmk_board_hil_mark(0xDEAD0000u | cpu);
			board_console_puts("SILICON_SMP_SMOKE: FAIL cpu");
			puts_u32(cpu);
			board_console_puts(" ran on ");
			puts_u32(g_seen[cpu] - 1u);
			board_console_putc('\n');
			silicon_smp_smoke_done();
			ulmk_thread_exit();
		}
		board_console_puts("SILICON_SMP_SMOKE: cpu");
		puts_u32(cpu);
		board_console_puts(" ok\n");
	}

	ulmk_board_hil_mark(0x5A11u);
	board_console_puts("SILICON_SMP_SMOKE: PASS\n");
	silicon_smp_smoke_done();
	ulmk_thread_exit();
}
