/* SPDX-License-Identifier: MIT */
/*
 * silicon_smp_smoke — TC275 HIL: CPU1 alive + affinity pin + IPI wake.
 *
 * RAM-log only; no ASCLIN / notif heap (isolates affinity + IPI path).
 *
 * Shared flags MUST live in the component domain (ULMK_PRIVATE): .bss alone
 * lands in kernel RAM (PRS0-only) and kills DRIVER threads on touch.
 */

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

void ulmk_board_hil_mark(uint32_t n);

__attribute__((weak)) void ulmk_board_hil_mark(uint32_t n)
{
	(void)n;
}

extern volatile uint32_t g_ulmk_console_log_len;
extern volatile char g_ulmk_console_log[];

#define CONSOLE_LOG_SIZE	2048u

void __attribute__((noinline)) silicon_smp_smoke_done(void);

static ULMK_PRIVATE volatile uint32_t g_seen_cpu1;

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

static void worker_cpu1(void *arg)
{
	(void)arg;
	g_seen_cpu1 = ulmk_cpu_id();
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

	attr.name       = "w1";
	attr.entry      = worker_cpu1;
	attr.arg        = NULL;
	attr.priority   = 1u;
	attr.stack_size = 2048u;
	attr.privilege  = ULMK_PRIV_DRIVER;
	attr.heap_size  = 0u;
	attr.cpu        = 1u;
	ulmk_board_hil_mark(0xC100u | (uint32_t)attr.cpu);

	tid = ulmk_thread_create(&attr);
	if (tid == ULMK_TID_INVALID || (int32_t)tid < 0) {
		ulmk_board_hil_mark(0xDEADu);
		ram_puts("SILICON_SMP_SMOKE: FAIL spawn\n");
		silicon_smp_smoke_done();
		ulmk_thread_exit();
	}
	ulmk_board_hil_mark(0xC200u);

	for (i = 0u; i < 400000u && g_seen_cpu1 == 0u; i++)
		ulmk_thread_yield();

	if (g_seen_cpu1 != 1u) {
		ulmk_board_hil_mark(0xDEADu);
		ram_puts("SILICON_SMP_SMOKE: FAIL cpu1 not seen\n");
		silicon_smp_smoke_done();
		ulmk_thread_exit();
	}

	ulmk_board_hil_mark(0x5A11u);
	ram_puts("SILICON_SMP_SMOKE: PASS\n");
	silicon_smp_smoke_done();
	ulmk_thread_exit();
}
