/* SPDX-License-Identifier: MIT */
/*
 * silicon_smp_smoke — HIL: every secondary alive + affinity pin + IPI wake.
 *
 * Sweeps every secondary the board declares rather than just the first one:
 * each core has its own SRC routing and CSA pool, so CPU1 running says
 * nothing about CPU2.  dev.py forces ULMK_CONFIG_ENABLE_SMP for this case.
 *
 * RAM-log only; no ASCLIN / notif heap (isolates affinity + IPI path).
 *
 * Shared flags MUST live in the component domain (ULMK_PRIVATE): .bss alone
 * lands in kernel RAM (PRS0-only) and kills DRIVER threads on touch.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk/platform.h>

/* Board snapshot may say nothing; a machine we cannot ask has one core. */
#ifndef ULMK_ARCH_NUM_CPU
#define ULMK_ARCH_NUM_CPU	1
#endif

void ulmk_board_hil_mark(uint32_t n);

__attribute__((weak)) void ulmk_board_hil_mark(uint32_t n)
{
	(void)n;
}

extern volatile uint32_t g_ulmk_console_log_len;
extern volatile char g_ulmk_console_log[];

#define CONSOLE_LOG_SIZE	2048u

void __attribute__((noinline)) silicon_smp_smoke_done(void);

/*
 * One slot per core, holding cpu_id + 1 so that 0 stays the "has not run yet"
 * state the poll waits on.  Storing the id rather than a flag catches a
 * worker that runs on the wrong core instead of just counting it as alive.
 */
static ULMK_PRIVATE volatile uint32_t g_seen[ULMK_ARCH_NUM_CPU];

static void ram_putc(char c)
{
	uint32_t n = g_ulmk_console_log_len;

	if (n >= CONSOLE_LOG_SIZE - 1u)
		return;
	g_ulmk_console_log[n] = c;
	g_ulmk_console_log_len = n + 1u;
}

static void ram_puts(const char *s)
{
	if (!s)
		return;
	while (*s)
		ram_putc(*s++);
}

static void ram_u32(uint32_t v)
{
	char buf[12];
	uint32_t n = 0u;

	do {
		buf[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while (v != 0u);

	while (n--)
		ram_putc(buf[n]);
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

	(void)info;
	g_ulmk_console_log_len = 0u;
	ulmk_board_hil_mark(1u);

	ram_puts("SILICON_SMP_SMOKE: begin\n");
	ulmk_board_hil_mark(3u);

	if (ulmk_cpu_id() != 0u) {
		ram_puts("SILICON_SMP_SMOKE: FAIL root cpu\n");
		ulmk_board_hil_mark(0xDEADu);
		silicon_smp_smoke_done();
		ulmk_thread_exit();
	}
	ulmk_board_hil_mark(6u);

	ram_puts("SILICON_SMP_SMOKE: cpus=");
	ram_u32((uint32_t)ULMK_ARCH_NUM_CPU);
	ram_putc('\n');

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
			ram_puts("SILICON_SMP_SMOKE: FAIL spawn cpu");
			ram_u32(cpu);
			ram_putc('\n');
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
			ram_puts("SILICON_SMP_SMOKE: FAIL cpu");
			ram_u32(cpu);
			ram_puts(" not seen\n");
			silicon_smp_smoke_done();
			ulmk_thread_exit();
		}
		if (g_seen[cpu] - 1u != cpu) {
			ulmk_board_hil_mark(0xDEAD0000u | cpu);
			ram_puts("SILICON_SMP_SMOKE: FAIL cpu");
			ram_u32(cpu);
			ram_puts(" ran on ");
			ram_u32(g_seen[cpu] - 1u);
			ram_putc('\n');
			silicon_smp_smoke_done();
			ulmk_thread_exit();
		}
		ram_puts("SILICON_SMP_SMOKE: cpu");
		ram_u32(cpu);
		ram_puts(" ok\n");
	}

	ulmk_board_hil_mark(0x5A11u);
	ram_puts("SILICON_SMP_SMOKE: PASS\n");
	silicon_smp_smoke_done();
	ulmk_thread_exit();
}
