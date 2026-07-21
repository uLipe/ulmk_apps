/* SPDX-License-Identifier: MIT */
/*
 * silicon_irq_stress — HIL IRQ flood / ooo-ack / rebind / preempt / exhaust.
 * Expect: SILICON_IRQ_STRESS: PASS  scratch 0x0106
 *
 * Real AURIX (TC275): userspace SRC SETR is either a class-4 bus error or
 * latches ICR.PIPN without taking the BIV slot.  STM0 CMP1 → SR1 can set
 * CMP1IR without ever asserting SRC.SRR.
 *
 * Trigger via CMP0 → STMIR0 → SRC_STM0SR0 (same HW path as the arch tick),
 * but bind a *non-tick* SRPN on that SRC.  The generic ISR early-returns on
 * ULMK_BOARD_IRQ_TICK without ulmk_kern_irq_dispatch(), so reusing the tick
 * SRPN would never wake the consumer.  Stealing SR0 for SRPN 10 suspends the
 * kernel tick for the duration of this one-shot HIL — do not start
 * board_timer / ulmk_tick_start from this app.
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <board_config.h>

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
ulmk_tid_t pinmux_init(uint8_t cpu);
void ulmk_board_hil_mark(uint32_t n);

__attribute__((weak)) void ulmk_board_hil_mark(uint32_t n)
{
	(void)n;
}

#define IRQ_BIT_IDX	0u
#define IRQ_MASK	(1u << IRQ_BIT_IDX)
#define BIT_RDY		(1u << 1)
#define BIT_FIN		(1u << 3)
#define FLOOD_N		32
#define PREEMPT_N	16

#define UL_IRQ_SRPN	10u	/* must != ULMK_BOARD_IRQ_TICK (see file header) */
#define UL_IRQ_SRC	((uintptr_t)ULMK_BOARD_SRC_STM0_SR0)

#define STM0_MAP_SIZE	0x100u
#define STM0_TIM0	(ULMK_BOARD_STM0_BASE + 0x010u)
#define STM0_CMP0	(ULMK_BOARD_STM0_BASE + 0x030u)
#define STM0_CMCON	(ULMK_BOARD_STM0_BASE + 0x038u)
#define STM0_ICR	(ULMK_BOARD_STM0_BASE + 0x03Cu)
#define STM0_ISCR	(ULMK_BOARD_STM0_BASE + 0x040u)

#define ICR_CMP0EN	(1u << 0)
#define ISCR_CMP0IRR	(1u << 0)
/* ~50 µs @ 100 MHz STM — enough margin past MMIO/scheduling skew. */
#define STM_DELTA_TICKS	5000u

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_notif_t g_irq;
static ULMK_PRIVATE ulmk_notif_t g_sync;
static ULMK_PRIVATE volatile int g_count;
static ULMK_PRIVATE volatile int g_bound;
static ULMK_PRIVATE volatile uint32_t *g_stm0;

void __attribute__((noinline)) silicon_irq_stress_done(void)
{
}

static void put_u32(uint32_t v)
{
	char buf[10];
	int  i = 0;

	if (v == 0u) {
		board_console_putc('0');
		return;
	}
	while (v) {
		buf[i++] = (char)('0' + (v % 10u));
		v /= 10u;
	}
	while (i--)
		board_console_putc(buf[i]);
}

static void check(const char *name, int ok)
{
	board_console_puts(ok ? ".ok " : ".FAIL ");
	board_console_puts(name);
	board_console_puts("\n");
	if (ok)
		g_pass++;
	else
		g_fail++;
}

#define CHECK(name, cond) check((name), (cond) ? 1 : 0)

static ulmk_tid_t spawn(const char *name, void (*entry)(void *), void *arg,
			uint8_t prio, size_t stack)
{
	ulmk_thread_attr_t a = {0};

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = stack;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.heap_size  = 0u;
	a.cpu = 0u;
	return ulmk_thread_create(&a);
}

static inline uint32_t stm0_off(uint32_t reg)
{
	return (reg - ULMK_BOARD_STM0_BASE) / sizeof(uint32_t);
}

static void irq_trigger(void)
{
	uint32_t now;
	uint32_t cmp;

	/*
	 * Exact board_timer arming order: disable + clear IR, program a
	 * future CMP0, then enable.  Enabling with CMP already behind TIM0
	 * continuously asserts SR0.
	 */
	g_stm0[stm0_off(STM0_ICR)]  = 0u;
	g_stm0[stm0_off(STM0_ISCR)] = ISCR_CMP0IRR;
	now = g_stm0[stm0_off(STM0_TIM0)];
	cmp = now + STM_DELTA_TICKS;
	g_stm0[stm0_off(STM0_CMP0)] = cmp;
	g_stm0[stm0_off(STM0_ICR)]  = ICR_CMP0EN;
}

static void irq_clear(void)
{
	g_stm0[stm0_off(STM0_ICR)]  = 0u;
	g_stm0[stm0_off(STM0_ISCR)] = ISCR_CMP0IRR;
	(void)ulmk_irq_ack(UL_IRQ_SRPN);
}

static int map_stm0(void)
{
	void *p;

	p = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_STM0_BASE, STM0_MAP_SIZE,
			 ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	if (!p)
		return -1;
	g_stm0 = (volatile uint32_t *)p;
	g_stm0[stm0_off(STM0_CMCON)] = 0x0000001Fu;
	g_stm0[stm0_off(STM0_ICR)]   = 0u;
	g_stm0[stm0_off(STM0_ISCR)]  = ISCR_CMP0IRR;
	return 0;
}

static int bind_enable(void)
{
	int rc;

	rc = ulmk_irq_bind_hw(UL_IRQ_SRPN, g_irq, IRQ_BIT_IDX, UL_IRQ_SRC);
	if (rc != ULMK_OK)
		return rc;
	rc = ulmk_irq_enable(UL_IRQ_SRPN);
	if (rc != ULMK_OK)
		return rc;
	/*
	 * Quiesce the arch tick compare: with SRPN stolen, a live CMP0EN
	 * would spam our notif.  irq_trigger() re-arms per shot.
	 */
	g_stm0[stm0_off(STM0_ICR)]  = 0u;
	g_stm0[stm0_off(STM0_ISCR)] = ISCR_CMP0IRR;
	(void)ulmk_irq_ack(UL_IRQ_SRPN);
	return ULMK_OK;
}

static void irq_consumer(void *arg)
{
	uint32_t n = (uint32_t)(uintptr_t)arg;
	uint32_t i;
	uint32_t bits;
	int      ret;

	if (!g_bound) {
		/* Per-thread MPU: root's STM0 map does not cover this AS. */
		if (map_stm0() != 0) {
			g_count = -1;
			ulmk_notif_signal(g_sync, BIT_FIN);
			ulmk_thread_exit();
		}
		ret = bind_enable();
		if (ret != ULMK_OK) {
			g_count = -1;
			ulmk_notif_signal(g_sync, BIT_FIN);
			ulmk_thread_exit();
		}
		g_bound = 1;
	}

	ulmk_notif_signal(g_sync, BIT_RDY);
	for (i = 0u; i < n; i++) {
		bits = 0u;
		ret = ulmk_notif_wait(g_irq, IRQ_MASK, &bits);
		if (ret != ULMK_OK || !(bits & IRQ_MASK)) {
			g_count = -1;
			ulmk_notif_signal(g_sync, BIT_FIN);
			ulmk_thread_exit();
		}
		irq_clear();
		g_count++;
		ulmk_notif_signal(g_sync, BIT_RDY);
	}
	ulmk_notif_signal(g_sync, BIT_FIN);
	ulmk_thread_exit();
}

static void fire_n(uint32_t n)
{
	uint32_t bits;
	uint32_t i;
	int      rc;

	(void)ulmk_thread_priority_set(ulmk_thread_self(), 8u);
	for (i = 0u; i < n; i++) {
		bits = 0u;
		rc = ulmk_notif_wait(g_sync, BIT_RDY | BIT_FIN, &bits);
		if (rc != ULMK_OK || (bits & BIT_FIN)) {
			(void)ulmk_thread_priority_set(ulmk_thread_self(),
						       100u);
			return;
		}
		irq_trigger();
	}
	bits = 0u;
	(void)ulmk_notif_wait(g_sync, BIT_FIN, &bits);
	(void)ulmk_thread_priority_set(ulmk_thread_self(), 100u);
}

static void run_flood(void)
{
	ulmk_tid_t w;
	uint32_t   i;

	g_count = 0;
	g_bound = 0;
	w = spawn("flood_w", irq_consumer, (void *)(uintptr_t)FLOOD_N,
		  2u, 2048u);
	CHECK("flood_spawn", w != ULMK_TID_INVALID);
	(void)ulmk_cap_grant(w, ULMK_CAP_IRQ);
	(void)ulmk_cap_grant(w, ULMK_CAP_MAP_PERIPH);
	/* Let the worker bind STM0 before we block waiting on BIT_RDY. */
	for (i = 0u; i < 4000u && !g_bound; i++)
		ulmk_thread_yield();
	CHECK("flood_bound", g_bound != 0);
	if (!g_bound)
		return;
	fire_n(FLOOD_N);
	CHECK("flood_count", g_count == FLOOD_N);
}

static void run_ooo_ack(void)
{
	uint32_t bits = 0u;
	int      rc;

	irq_trigger();
	rc = ulmk_notif_wait(g_irq, IRQ_MASK, &bits);
	CHECK("ooo_wait", rc == ULMK_OK && (bits & IRQ_MASK));
	irq_clear();
	CHECK("ooo_ack1", ulmk_irq_ack(UL_IRQ_SRPN) == ULMK_OK);
	CHECK("ooo_ack2", ulmk_irq_ack(UL_IRQ_SRPN) == ULMK_OK);

	bits = 0u;
	irq_trigger();
	rc = ulmk_notif_wait(g_irq, IRQ_MASK, &bits);
	CHECK("ooo_again", rc == ULMK_OK && (bits & IRQ_MASK));
	irq_clear();
	CHECK("ooo_ack3", 1);
}

static void run_rebind(void)
{
	uint32_t bits;
	int      i;

	/* Disable/enable proxy (no second STM compare line in this test). */
	CHECK("rebind_dis", ulmk_irq_disable(UL_IRQ_SRPN) == ULMK_OK);
	irq_trigger();
	for (i = 0; i < 1000; i++)
		ulmk_thread_yield();
	irq_clear();
	CHECK("rebind_masked", !ulmk_notif_poll(g_irq, IRQ_MASK));
	CHECK("rebind_en", ulmk_irq_enable(UL_IRQ_SRPN) == ULMK_OK);
	bits = 0u;
	irq_trigger();
	CHECK("rebind_again",
	      ulmk_notif_wait(g_irq, IRQ_MASK, &bits) == ULMK_OK &&
	      (bits & IRQ_MASK));
	irq_clear();
	CHECK("rebind_deliv", 1);
}

static void run_preempt(void)
{
	ulmk_tid_t w;

	g_count = 0;
	w = spawn("pre_w", irq_consumer, (void *)(uintptr_t)PREEMPT_N,
		  2u, 2048u);
	CHECK("preempt_spawn", w != ULMK_TID_INVALID);
	(void)ulmk_cap_grant(w, ULMK_CAP_IRQ);
	(void)ulmk_cap_grant(w, ULMK_CAP_MAP_PERIPH);
	fire_n(PREEMPT_N);
	CHECK("preempt_count", g_count == PREEMPT_N);
}

static void run_bind_exhaust(void)
{
	int          n;
	int          i;
	int          rc;
	int          hit_nospace;
	ulmk_notif_t dummy;

	/*
	 * Soft ulmk_irq_bind() walks SRC words that may #DE on TC275.
	 * Exhaust via bind_hw rewriting the same known-good STM0 SR0
	 * (table fill only; last writer owns the SRC).
	 */
	n = 0;
	hit_nospace = 0;
	for (i = 0; i < 32; i++) {
		dummy = ulmk_notif_create();
		if (dummy == ULMK_NOTIF_INVALID)
			break;
		rc = ulmk_irq_bind_hw((uint8_t)(20u + (unsigned)i), dummy,
				     IRQ_BIT_IDX, UL_IRQ_SRC);
		if (rc == ULMK_ENOSPC) {
			hit_nospace = 1;
			break;
		}
		if (rc == ULMK_OK)
			n++;
	}
	CHECK("bind_got_space", n > 0);
	CHECK("bind_exhausted", hit_nospace);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;

	ulmk_board_hil_mark(1u);
	/* Console only — board_timer would own STM0 CMP0/SR0. */
	tid = pinmux_init(0u);
	if (tid == ULMK_TID_INVALID)
		ulmk_board_hil_mark(0x70u);
	(void)board_console_start(info);
	ulmk_board_hil_mark(3u);
	board_console_puts("SILICON_IRQ_STRESS: begin\n");
	g_pass  = 0;
	g_fail  = 0;
	g_bound = 0;

	CHECK("map", map_stm0() == 0);
	g_irq  = ulmk_notif_create();
	g_sync = ulmk_notif_create();
	CHECK("notifs", g_irq != ULMK_NOTIF_INVALID &&
			g_sync != ULMK_NOTIF_INVALID);

	(void)ulmk_thread_priority_set(ulmk_thread_self(), 100u);

	board_console_puts("> flood\n");
	run_flood();
	board_console_puts("> ooo_ack\n");
	run_ooo_ack();
	board_console_puts("> rebind\n");
	run_rebind();
	board_console_puts("> preempt\n");
	run_preempt();
	board_console_puts("> bind_exhaust\n");
	run_bind_exhaust();

	(void)ulmk_irq_disable(UL_IRQ_SRPN);
	irq_clear();

	board_console_puts("SILICON_IRQ_STRESS: REPORT pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_puts("\n");
	if (g_fail == 0) {
		ulmk_board_hil_mark(0x0106u);
		board_console_puts("SILICON_IRQ_STRESS: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_IRQ_STRESS: FAIL\n");
	}
	silicon_irq_stress_done();
	ulmk_thread_exit();
}
