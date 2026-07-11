/* SPDX-License-Identifier: MIT */
/*
 * silicon_stress — certification stress / metrics on silicon (ulmk_apps).
 *
 * Measures context-switch cost, syscall WCET samples, IPC throughput, heap
 * exhaustion, and MPU isolation (grant + recoverable deny).  Prints a
 * parseable REPORT then SILICON_STRESS: PASS/FAIL for HIL.
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void board_timer_sleep_us(uint32_t us);
uint32_t board_timer_now_ticks(void);
uint32_t board_timer_ticks_to_ns(uint32_t dt);
void ulmk_board_hil_mark(uint32_t n);

extern uint8_t _ulmk_kernel_text_start[];
extern uint8_t _ulmk_kernel_text_end[];
extern uint8_t _ulmk_user_ram_start[];
extern uint8_t _ulmk_user_pool_start[];
extern uint8_t _ulmk_user_pool_end[];
extern uint8_t _ulmk_csa_pool_start[];
extern uint8_t _ulmk_csa_pool_end[];

#ifndef ULMK_BOARD_FCPU_HZ
#define ULMK_BOARD_FCPU_HZ	200000000u
#endif
#ifndef ULMK_BOARD_FSTM_HZ
#define ULMK_BOARD_FSTM_HZ	100000000u
#endif
#ifndef ULMK_BOARD_TRICORE_ISA_MAJOR
#define ULMK_BOARD_TRICORE_ISA_MAJOR	1
#endif
#ifndef ULMK_BOARD_TRICORE_ISA_MINOR
#define ULMK_BOARD_TRICORE_ISA_MINOR	6
#endif
#ifndef ULMK_BOARD_TRICORE_ISA_PATCH
#define ULMK_BOARD_TRICORE_ISA_PATCH	1
#endif

#define STRESS_CTX_ROUNDS		50u
#define STRESS_SYSCALL_ITERS		100u
#define STRESS_IPC_WINDOW_MS		50u
#define STRESS_HEAP_CHUNK		256u

static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE int g_timer_ok;
static ULMK_PRIVATE int g_isolation_grant_ok;
static ULMK_PRIVATE int g_isolation_deny_ok;
static ULMK_PRIVATE int g_heap_ok;
static ULMK_PRIVATE uint32_t g_heap_bytes;
static ULMK_PRIVATE uint32_t g_ctx_min_ns;
static ULMK_PRIVATE uint32_t g_ctx_avg_ns;
static ULMK_PRIVATE uint32_t g_ctx_max_ns;
static ULMK_PRIVATE uint32_t g_yield_min_ns;
static ULMK_PRIVATE uint32_t g_yield_avg_ns;
static ULMK_PRIVATE uint32_t g_yield_max_ns;
static ULMK_PRIVATE uint32_t g_self_min_ns;
static ULMK_PRIVATE uint32_t g_self_avg_ns;
static ULMK_PRIVATE uint32_t g_self_max_ns;
static ULMK_PRIVATE uint32_t g_ipc_calls;
static ULMK_PRIVATE uint32_t g_ipc_per_s;
static ULMK_PRIVATE uint32_t g_ipc_ns_per_call;

static ULMK_PRIVATE ulmk_ep_t g_ipc_ep;
static ULMK_PRIVATE volatile int g_peer_ready;
static ULMK_PRIVATE volatile int g_peer_done;
static ULMK_PRIVATE volatile uint32_t *g_shared;
static ULMK_PRIVATE volatile int g_victim_ran;

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

static void put_kv_u32(const char *k, uint32_t v)
{
	board_console_puts(k);
	board_console_puts("=");
	put_u32(v);
	board_console_putc('\n');
}

static void stats_reset(uint32_t *mn, uint32_t *avg, uint32_t *mx)
{
	*mn  = 0xFFFFFFFFu;
	*avg = 0u;
	*mx  = 0u;
}

static void stats_acc(uint32_t *mn, uint64_t *sum, uint32_t *mx, uint32_t sample)
{
	if (sample < *mn)
		*mn = sample;
	if (sample > *mx)
		*mx = sample;
	*sum += sample;
}

static ulmk_tid_t spawn(const char *name, void (*entry)(void *), void *arg,
			uint8_t prio, size_t stack, size_t heap)
{
	ulmk_thread_attr_t a;

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = stack;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.heap_size  = heap;
	return ulmk_thread_create(&a);
}

/* ── Timer smoke ─────────────────────────────────────────────────────────── */

static void bench_timer(void)
{
	uint32_t t0;
	uint32_t t1;
	uint32_t dt;

	t0 = board_timer_now_ticks();
	board_timer_sleep_us(1000u);
	t1 = board_timer_now_ticks();
	dt = t1 - t0;
	g_timer_ok = (dt > (ULMK_BOARD_FSTM_HZ / 5000u)) &&
		     (dt < (ULMK_BOARD_FSTM_HZ / 200u));
	if (!g_timer_ok)
		g_fail++;
}

/* ── Context switch (yield ping-pong) ────────────────────────────────────── */

static void ctx_spinner(void *arg)
{
	(void)arg;
	g_peer_ready = 1;
	for (;;)
		ulmk_thread_yield();
}

static void bench_ctx_switch(void)
{
	uint32_t i;
	uint32_t t0;
	uint32_t t1;
	uint32_t ns;
	uint32_t mn;
	uint32_t mx;
	uint64_t sum;
	ulmk_tid_t peer;

	g_peer_ready = 0;
	/*
	 * Same priority as root: yield only rotates among equal prios.
	 * Never board_timer_sleep_us() while this spinner is ready — sleep
	 * blocks root and the prio-0 spinner starves the lower-prio timer
	 * server (yield is a no-op when no equal-prio peer is runnable).
	 */
	peer = spawn("ctx_s", ctx_spinner, NULL, 0u, 1024u, 0u);
	if (peer == ULMK_TID_INVALID) {
		g_fail++;
		return;
	}

	for (i = 0u; i < 1000u && !g_peer_ready; i++)
		ulmk_thread_yield();
	if (!g_peer_ready) {
		g_fail++;
		ulmk_thread_kill(peer);
		return;
	}

	stats_reset(&mn, &g_ctx_avg_ns, &mx);
	sum = 0u;
	for (i = 0u; i < STRESS_CTX_ROUNDS; i++) {
		t0 = board_timer_now_ticks();
		ulmk_thread_yield();
		t1 = board_timer_now_ticks();
		ns = board_timer_ticks_to_ns(t1 - t0) / 2u;
		stats_acc(&mn, &sum, &mx, ns);
	}

	g_ctx_min_ns = mn;
	g_ctx_max_ns = mx;
	g_ctx_avg_ns = (uint32_t)(sum / STRESS_CTX_ROUNDS);
	ulmk_thread_kill(peer);
}

/* ── Syscall WCET samples ────────────────────────────────────────────────── */

static void bench_syscalls(void)
{
	uint32_t i;
	uint32_t t0;
	uint32_t t1;
	uint32_t ns;
	uint32_t mn;
	uint32_t mx;
	uint64_t sum;

	stats_reset(&mn, &g_yield_avg_ns, &mx);
	sum = 0u;
	for (i = 0u; i < STRESS_SYSCALL_ITERS; i++) {
		t0 = board_timer_now_ticks();
		(void)ulmk_thread_yield();
		t1 = board_timer_now_ticks();
		ns = board_timer_ticks_to_ns(t1 - t0);
		stats_acc(&mn, &sum, &mx, ns);
	}
	g_yield_min_ns = mn;
	g_yield_max_ns = mx;
	g_yield_avg_ns = (uint32_t)(sum / STRESS_SYSCALL_ITERS);

	stats_reset(&mn, &g_self_avg_ns, &mx);
	sum = 0u;
	for (i = 0u; i < STRESS_SYSCALL_ITERS; i++) {
		t0 = board_timer_now_ticks();
		(void)ulmk_thread_self();
		t1 = board_timer_now_ticks();
		ns = board_timer_ticks_to_ns(t1 - t0);
		stats_acc(&mn, &sum, &mx, ns);
	}
	g_self_min_ns = mn;
	g_self_max_ns = mx;
	g_self_avg_ns = (uint32_t)(sum / STRESS_SYSCALL_ITERS);
}

static uint32_t us_to_ticks_local(uint32_t us)
{
	uint64_t ticks;

	ticks = ((uint64_t)us * (uint64_t)ULMK_BOARD_FSTM_HZ) / 1000000u;
	if (ticks == 0u)
		ticks = 1u;
	if (ticks > 0xFFFFFFFFu)
		return 0xFFFFFFFFu;
	return (uint32_t)ticks;
}

/* ── IPC throughput ──────────────────────────────────────────────────────── */

static void ipc_server(void *arg)
{
	ulmk_ep_t  ep = g_ipc_ep;
	ulmk_msg_t m;
	ulmk_tid_t snd;

	(void)arg;
	g_peer_ready = 1;
	for (;;) {
		if (ulmk_ep_recv(ep, &m, &snd) != ULMK_OK)
			break;
		if (m.label == 0xDEADu) {
			ulmk_ep_reply(snd, &m);
			break;
		}
		m.label++;
		ulmk_ep_reply(snd, &m);
	}
	g_peer_done = 1;
	ulmk_thread_exit();
}

static void bench_ipc(void)
{
	ulmk_msg_t m;
	ulmk_tid_t srv;
	uint32_t   t0;
	uint32_t   t1;
	uint32_t   window;
	uint32_t   calls;
	int        rc;
	uint32_t   waits;

	g_ipc_ep     = ulmk_ep_create();
	g_peer_ready = 0;
	g_peer_done  = 0;
	/* Same prio as root so yield can schedule the server. */
	srv = spawn("ipc_s", ipc_server, NULL, 0u, 1024u, 0u);
	if (srv == ULMK_TID_INVALID || g_ipc_ep == ULMK_EP_INVALID) {
		g_fail++;
		return;
	}
	ulmk_ep_grant(g_ipc_ep, srv);

	waits = 0u;
	while (!g_peer_ready && waits < 50u) {
		board_timer_sleep_us(100u);
		waits++;
	}
	if (!g_peer_ready) {
		g_fail++;
		ulmk_thread_kill(srv);
		ulmk_ep_destroy(g_ipc_ep);
		return;
	}

	window = us_to_ticks_local(STRESS_IPC_WINDOW_MS * 1000u);
	calls  = 0u;
	t0     = board_timer_now_ticks();
	for (;;) {
		t1 = board_timer_now_ticks();
		if ((t1 - t0) >= window)
			break;
		if (calls >= 1000000u)
			break;
		m.label = 0x100u;
		rc = ulmk_ep_call(g_ipc_ep, &m);
		if (rc != ULMK_OK || m.label != 0x101u) {
			g_fail++;
			break;
		}
		calls++;
	}
	g_ipc_calls = calls;
	if (STRESS_IPC_WINDOW_MS > 0u)
		g_ipc_per_s = (calls * 1000u) / STRESS_IPC_WINDOW_MS;
	if (calls > 0u)
		g_ipc_ns_per_call = board_timer_ticks_to_ns(t1 - t0) / calls;

	m.label = 0xDEADu;
	(void)ulmk_ep_call(g_ipc_ep, &m);
	waits = 0u;
	while (!g_peer_done && waits < 50u) {
		board_timer_sleep_us(100u);
		waits++;
	}
	ulmk_thread_kill(srv);
	ulmk_ep_destroy(g_ipc_ep);
}

/* ── Heap exhaustion ─────────────────────────────────────────────────────── */

static void bench_heap(void)
{
	void    *blocks[64];
	uint32_t n;
	uint32_t bytes;
	void    *p;

	n     = 0u;
	bytes = 0u;
	for (;;) {
		if (n >= 64u)
			break;
		p = ulmk_mem_map(NULL, STRESS_HEAP_CHUNK,
				 ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_ANON);
		if (!p)
			break;
		blocks[n++] = p;
		bytes += STRESS_HEAP_CHUNK;
	}

	g_heap_bytes = bytes;
	g_heap_ok    = (n > 0u);
	if (!g_heap_ok)
		g_fail++;

	while (n > 0u) {
		n--;
		(void)ulmk_mem_unmap(blocks[n], STRESS_HEAP_CHUNK);
	}
}

/* ── Isolation ───────────────────────────────────────────────────────────── */

static void grant_worker(void *arg)
{
	(void)arg;
	while (!g_shared)
		ulmk_thread_yield();
	*g_shared = 0xA5A5A5A5u;
	g_peer_done = 1;
	ulmk_thread_exit();
}

static void deny_victim(void *arg)
{
	volatile uint32_t *kernel_touch;

	(void)arg;
	g_victim_ran = 1;
	/*
	 * Coarse DPR2 covers all user RAM, so anon-without-grant will not
	 * trap.  Touch kernel SRAM below user_ram — PRS1 has no DPR1 → Class 1
	 * → recoverable kill.
	 */
	kernel_touch = (volatile uint32_t *)
		((uintptr_t)_ulmk_user_ram_start - 64u);
	*kernel_touch = 0xBAD0BAD0u;
	g_victim_ran = 2;
	ulmk_thread_exit();
}

static void bench_isolation(void)
{
	volatile uint32_t *buf;
	ulmk_tid_t         w;
	ulmk_tid_t         v;

	g_shared    = NULL;
	g_peer_done = 0;
	w = spawn("igrnt", grant_worker, NULL, 0u, 1024u, 0u);

	buf = (volatile uint32_t *)ulmk_mem_map(
		NULL, 256u, ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_ANON);
	if (!buf) {
		g_fail++;
		g_isolation_grant_ok = 0;
	} else {
		if (ulmk_mem_grant((void *)buf, 256u, w,
				   ULMK_PERM_READ | ULMK_PERM_WRITE) != ULMK_OK) {
			g_fail++;
			g_isolation_grant_ok = 0;
		} else {
			g_shared = buf;
			/* Equal-prio yield — avoid sleep while peer may be spinning. */
			{
				uint32_t wi;

				for (wi = 0u; wi < 1000u && !g_peer_done; wi++)
					ulmk_thread_yield();
			}
			g_isolation_grant_ok = (g_peer_done && *buf == 0xA5A5A5A5u);
			if (!g_isolation_grant_ok)
				g_fail++;
		}
		(void)ulmk_mem_unmap((void *)buf, 256u);
		g_shared = NULL;
	}
	ulmk_thread_kill(w);

	g_victim_ran = 0;
	v = spawn("ideny", deny_victim, NULL, 0u, 1024u, 0u);
	/*
	 * Root must block (not spin-yield) so the equal-prio victim runs and
	 * takes the Class 1.  sleep blocks root; victim is highest ready.
	 */
	board_timer_sleep_us(5000u);
	/*
	 * Dead threads report ESRCH from priority_get.  g_victim_ran==1 means
	 * the fault path ran (never reached 2 after the store).
	 */
	g_isolation_deny_ok =
		(g_victim_ran == 1) &&
		(ulmk_thread_priority_get(v) == ULMK_ESRCH);
	if (!g_isolation_deny_ok)
		g_fail++;
}

/* ── Footprint helpers ───────────────────────────────────────────────────── */

static uint32_t ptr_diff(const uint8_t *a, const uint8_t *b)
{
	uintptr_t ua = (uintptr_t)a;
	uintptr_t ub = (uintptr_t)b;

	if (ub >= ua)
		return (uint32_t)(ub - ua);
	return 0u;
}

static void print_report(void)
{
	board_console_puts("SILICON_STRESS: REPORT\n");
	board_console_puts("arch=tricore isa=");
	put_u32(ULMK_BOARD_TRICORE_ISA_MAJOR);
	board_console_putc('.');
	put_u32(ULMK_BOARD_TRICORE_ISA_MINOR);
	board_console_putc('.');
	put_u32(ULMK_BOARD_TRICORE_ISA_PATCH);
	board_console_putc('\n');

	put_kv_u32("fcpu_hz", ULMK_BOARD_FCPU_HZ);
	put_kv_u32("fstm_hz", ULMK_BOARD_FSTM_HZ);
	put_kv_u32("flash_text_bytes",
		   ptr_diff(_ulmk_kernel_text_start, _ulmk_kernel_text_end));
	put_kv_u32("ram_user_bytes",
		   ptr_diff(_ulmk_user_ram_start, _ulmk_user_pool_end));
	put_kv_u32("heap_pool_bytes",
		   ptr_diff(_ulmk_user_pool_start, _ulmk_user_pool_end));
	/* Linker symbols only — boot_info lives in kernel RAM (not readable). */
	put_kv_u32("csa_pool_bytes",
		   ptr_diff(_ulmk_csa_pool_start, _ulmk_csa_pool_end));

	put_kv_u32("timer_sleep_ok", (uint32_t)g_timer_ok);
	put_kv_u32("ctx_switch_ns_min", g_ctx_min_ns);
	put_kv_u32("ctx_switch_ns_avg", g_ctx_avg_ns);
	put_kv_u32("ctx_switch_ns_max", g_ctx_max_ns);
	put_kv_u32("sys_yield_ns_min", g_yield_min_ns);
	put_kv_u32("sys_yield_ns_avg", g_yield_avg_ns);
	put_kv_u32("sys_yield_ns_max", g_yield_max_ns);
	put_kv_u32("sys_self_ns_min", g_self_min_ns);
	put_kv_u32("sys_self_ns_avg", g_self_avg_ns);
	put_kv_u32("sys_self_ns_max", g_self_max_ns);
	put_kv_u32("ipc_calls", g_ipc_calls);
	put_kv_u32("ipc_calls_per_s", g_ipc_per_s);
	put_kv_u32("ipc_ns_per_call", g_ipc_ns_per_call);
	put_kv_u32("heap_exhaust_ok", (uint32_t)g_heap_ok);
	put_kv_u32("heap_exhaust_bytes", g_heap_bytes);
	put_kv_u32("isolation_grant_ok", (uint32_t)g_isolation_grant_ok);
	put_kv_u32("isolation_deny_kill_ok", (uint32_t)g_isolation_deny_ok);
}

void __attribute__((noinline)) silicon_stress_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	/*
	 * Do not dereference info: it points into kernel RAM (PRS1 Class 1).
	 * Pass the opaque pointer to board_services_init only.
	 */
	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_STRESS: begin\n");

	board_console_puts("> timer\n");
	bench_timer();
	ulmk_board_hil_mark(4u);
	board_console_puts("> syscalls\n");
	bench_syscalls();
	ulmk_board_hil_mark(6u);
	board_console_puts("> heap\n");
	bench_heap();
	ulmk_board_hil_mark(8u);
	board_console_puts("> ctx_switch\n");
	bench_ctx_switch();
	ulmk_board_hil_mark(5u);
	board_console_puts("> ipc\n");
	bench_ipc();
	ulmk_board_hil_mark(7u);
	board_console_puts("> isolation\n");
	bench_isolation();
	ulmk_board_hil_mark(9u);

	print_report();

	if (g_fail == 0) {
		ulmk_board_hil_mark(0x57E55u);
		board_console_puts("SILICON_STRESS: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_STRESS: FAIL fails=");
		put_u32((uint32_t)g_fail);
		board_console_putc('\n');
	}

	silicon_stress_done();
	ulmk_thread_exit();
}
