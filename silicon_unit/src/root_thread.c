/* SPDX-License-Identifier: MIT */
/*
 * silicon_unit — HIL unit tests for public syscalls (ulmk_apps).
 *
 * Per syscall group: happy path, edge cases, and crash-hardening probes
 * (invalid handles / nullish args must return errors, never panic).
 *
 * Progress is printed as short section markers and .ok/.FAIL lines so the
 * 2048-byte RAM console log still holds the final REPORT.
 *
 * HIL: SILICON_UNIT: PASS  scratch 0x5017  silicon_unit_done().
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_tid_t g_target;
static ULMK_PRIVATE ulmk_ep_t g_ipc_ep;
static ULMK_PRIVATE ulmk_notif_t g_done;
static ULMK_PRIVATE volatile int g_heap_ok;
static ULMK_PRIVATE volatile int g_heap_ext_ok;
static ULMK_PRIVATE volatile int g_peer_ready;

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

static void progress(const char *section)
{
	board_console_puts("> ");
	board_console_puts(section);
	board_console_putc('\n');
}

static void check(const char *name, int ok)
{
	board_console_puts(ok ? ".ok " : ".FAIL ");
	board_console_puts(name);
	board_console_putc('\n');
	if (ok)
		g_pass++;
	else
		g_fail++;
}

#define CHECK(name, cond) check((name), (cond) ? 1 : 0)

static int map_ok(const void *p)
{
	uintptr_t u = (uintptr_t)p;

	if (u == 0u)
		return 0;
	if (u >= 0x80000000u)
		return 1;
	return (intptr_t)u > 0;
}

static ulmk_tid_t spawn(const char *name, void (*entry)(void *), void *arg,
			uint8_t prio, size_t heap)
{
	ulmk_thread_attr_t a;

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = 1024u;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.heap_size  = heap;
	return ulmk_thread_create(&a);
}

static void idle_target(void *arg)
{
	(void)arg;
	g_peer_ready = 1;
	for (;;)
		ulmk_thread_yield();
}

static void ipc_server(void *arg)
{
	ulmk_ep_t  ep = g_ipc_ep;
	ulmk_msg_t m;
	ulmk_msg_t next;
	ulmk_tid_t snd;

	(void)arg;
	if (ulmk_ep_recv(ep, &m, &snd) == ULMK_OK) {
		m.label += 1u;
		ulmk_ep_reply(snd, &m);
	}
	snd = ULMK_TID_INVALID;
	if (ulmk_ep_reply_recv(ep, snd, &m, &next, &snd) == ULMK_OK) {
		next.label += 1u;
		ulmk_ep_reply_recv(ep, snd, &next, &m, &snd);
	}
	ulmk_thread_exit();
}

static void heap_probe(void *arg)
{
	ulmk_heap_info_t hi;

	(void)arg;
	g_heap_ok = (ulmk_get_thread_heap(&hi) == ULMK_OK && hi.size > 0u);
	g_heap_ext_ok = (ulmk_heap_extend(128u) == ULMK_OK);
	ulmk_notif_signal(g_done, 0x1u);
	ulmk_thread_exit();
}

/* ── thread ─────────────────────────────────────────────────────────────── */

static void test_thread_happy(void)
{
	ulmk_tid_t self = ulmk_thread_self();

	progress("thr/happy");
	CHECK("self", self != ULMK_TID_INVALID);
	CHECK("yield", ulmk_thread_yield() == ULMK_OK);
	g_peer_ready = 0;
	g_target = spawn("tgt", idle_target, NULL, 200u, 0u);
	CHECK("create", g_target != ULMK_TID_INVALID);
	CHECK("get_prio", ulmk_thread_priority_get(g_target) == 200);
	CHECK("set_prio", ulmk_thread_priority_set(g_target, 150) == ULMK_OK);
	CHECK("get_prio2", ulmk_thread_priority_get(g_target) == 150);
	CHECK("suspend", ulmk_thread_suspend(g_target) == ULMK_OK);
	CHECK("resume", ulmk_thread_resume(g_target) == ULMK_OK);
}

static void test_thread_edge(void)
{
	ulmk_tid_t dead;
	ulmk_tid_t self = ulmk_thread_self();

	progress("thr/edge");
	CHECK("prio_self", ulmk_thread_priority_get(self) == 0);

	dead = spawn("die", idle_target, NULL, 200u, 0u);
	CHECK("kill", ulmk_thread_kill(dead) == ULMK_OK);
	ulmk_thread_yield();
	CHECK("get_dead", ulmk_thread_priority_get(dead) == ULMK_ESRCH);
	CHECK("kill_dead", ulmk_thread_kill(dead) == ULMK_ESRCH ||
	      ulmk_thread_kill(dead) == ULMK_EINVAL);
}

static void test_thread_crash(void)
{
	progress("thr/crash");
	CHECK("kill_inv", ulmk_thread_kill(ULMK_TID_INVALID) != ULMK_OK);
	CHECK("sus_inv", ulmk_thread_suspend(ULMK_TID_INVALID) != ULMK_OK);
	CHECK("res_inv", ulmk_thread_resume(ULMK_TID_INVALID) != ULMK_OK);
	CHECK("set_inv",
	      ulmk_thread_priority_set(ULMK_TID_INVALID, 1u) != ULMK_OK);
	CHECK("get_inv", ulmk_thread_priority_get(ULMK_TID_INVALID) < 0);
	CHECK("cap_inv",
	      ulmk_cap_grant(ULMK_TID_INVALID, ULMK_CAP_SPAWN) != ULMK_OK);
}

/* ── ipc ────────────────────────────────────────────────────────────────── */

static void test_ipc_happy(void)
{
	ulmk_ep_t  ep;
	ulmk_ep_t  tmp;
	ulmk_tid_t srv;
	ulmk_msg_t m;
	int        rc;

	progress("ipc/happy");
	ep = ulmk_ep_create();
	g_ipc_ep = ep;
	CHECK("ep_create", ep != ULMK_EP_INVALID);
	srv = spawn("ipc", ipc_server, NULL, 1u, 0u);
	CHECK("ep_grant", ulmk_ep_grant(ep, srv) == ULMK_OK);
	m.label = 0x100u;
	rc = ulmk_ep_call(ep, &m);
	CHECK("ep_call", rc == ULMK_OK && m.label == 0x101u);
	m.label = 0x200u;
	rc = ulmk_ep_call(ep, &m);
	CHECK("ep_reply_recv", rc == ULMK_OK && m.label == 0x201u);
	tmp = ulmk_ep_create();
	CHECK("ep_destroy", ulmk_ep_destroy(tmp) == ULMK_OK);
	ulmk_thread_kill(srv);
	ulmk_ep_destroy(ep);
}

static void test_ipc_edge(void)
{
	ulmk_ep_t    ep;
	ulmk_notif_t n;
	ulmk_msg_t   msg;
	ulmk_tid_t   sender;
	uint32_t     bits = 0u;
	int          rc;

	progress("ipc/edge");
	ep = ulmk_ep_create();
	n  = ulmk_notif_create();
	ulmk_notif_signal(n, 0x8u);
	rc = ulmk_ep_recv_or_notif(ep, n, 0x8u, &msg, &sender, &bits);
	CHECK("recv_or_notif", rc == 1 && bits == 0x8u);
	CHECK("ep_dbl_destroy",
	      ulmk_ep_destroy(ep) == ULMK_OK &&
	      ulmk_ep_destroy(ep) != ULMK_OK);
	ulmk_notif_destroy(n);
}

static void test_ipc_crash(void)
{
	ulmk_msg_t m;

	progress("ipc/crash");
	m.label = 0u;
	CHECK("call_inv", ulmk_ep_call(ULMK_EP_INVALID, &m) != ULMK_OK);
	CHECK("recv_inv",
	      ulmk_ep_recv(ULMK_EP_INVALID, &m, &g_target) != ULMK_OK);
	CHECK("reply_inv", ulmk_ep_reply(ULMK_TID_INVALID, &m) != ULMK_OK);
	CHECK("grant_inv",
	      ulmk_ep_grant(ULMK_EP_INVALID, g_target) != ULMK_OK);
	CHECK("dest_inv", ulmk_ep_destroy(ULMK_EP_INVALID) != ULMK_OK);
}

/* ── notif ──────────────────────────────────────────────────────────────── */

static void test_notif_happy(void)
{
	ulmk_notif_t n;
	uint32_t     bits = 0u;

	progress("notif/happy");
	n = ulmk_notif_create();
	CHECK("create", n != ULMK_NOTIF_INVALID);
	CHECK("signal", ulmk_notif_signal(n, 0x1u) == ULMK_OK);
	CHECK("poll", ulmk_notif_poll(n, 0x1u) == 0x1u);
	CHECK("signal2", ulmk_notif_signal(n, 0x2u) == ULMK_OK);
	CHECK("wait",
	      ulmk_notif_wait(n, 0x2u, &bits) == ULMK_OK && bits == 0x2u);
	CHECK("destroy", ulmk_notif_destroy(n) == ULMK_OK);
}

static void test_notif_edge(void)
{
	ulmk_notif_t n;

	progress("notif/edge");
	n = ulmk_notif_create();
	CHECK("poll_empty", ulmk_notif_poll(n, 0x1u) == 0u);
	CHECK("signal0", ulmk_notif_signal(n, 0u) == ULMK_OK ||
	      ulmk_notif_signal(n, 0u) == ULMK_EINVAL);
	CHECK("dbl_destroy",
	      ulmk_notif_destroy(n) == ULMK_OK &&
	      ulmk_notif_destroy(n) != ULMK_OK);
}

static void test_notif_crash(void)
{
	uint32_t bits = 0u;

	progress("notif/crash");
	CHECK("sig_inv",
	      ulmk_notif_signal(ULMK_NOTIF_INVALID, 0x1u) != ULMK_OK);
	CHECK("wait_inv",
	      ulmk_notif_wait(ULMK_NOTIF_INVALID, 0x1u, &bits) != ULMK_OK);
	CHECK("dest_inv",
	      ulmk_notif_destroy(ULMK_NOTIF_INVALID) != ULMK_OK);
	CHECK("poll_inv", ulmk_notif_poll(ULMK_NOTIF_INVALID, 0x1u) == 0u);
}

/* ── mem ────────────────────────────────────────────────────────────────── */

static void test_mem_happy(void)
{
	volatile uint32_t *p;

	progress("mem/happy");
	p = (volatile uint32_t *)ulmk_mem_map(
		NULL, 256u, ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_ANON);
	CHECK("map", map_ok((const void *)p));
	if (map_ok((const void *)p)) {
		p[0] = 0xdeadbeefu;
		CHECK("rw", p[0] == 0xdeadbeefu);
		CHECK("grant",
		      ulmk_mem_grant((void *)p, 256u, g_target,
				     ULMK_PERM_READ) == ULMK_OK);
		CHECK("unmap", ulmk_mem_unmap((void *)p, 256u) == ULMK_OK);
	} else {
		check("rw", 0);
		check("grant", 0);
		check("unmap", 0);
	}
}

static void test_mem_edge(void)
{
	void *p;

	progress("mem/edge");
	p = ulmk_mem_map(NULL, 64u, ULMK_PERM_READ | ULMK_PERM_WRITE,
			 ULMK_MMAP_ANON);
	CHECK("map64", map_ok(p));
	if (map_ok(p))
		CHECK("unmap64", ulmk_mem_unmap(p, 64u) == ULMK_OK);
	p = ulmk_mem_map(NULL, 0u, ULMK_PERM_READ | ULMK_PERM_WRITE,
			 ULMK_MMAP_ANON);
	CHECK("map0", !map_ok(p));
}

static void test_mem_crash(void)
{
	progress("mem/crash");
	CHECK("unmap_null", ulmk_mem_unmap(NULL, 64u) != ULMK_OK);
	CHECK("unmap_bad",
	      ulmk_mem_unmap((void *)(uintptr_t)0x1u, 64u) != ULMK_OK);
	CHECK("grant_null",
	      ulmk_mem_grant(NULL, 64u, g_target, ULMK_PERM_READ) != ULMK_OK);
	CHECK("grant_dead",
	      ulmk_mem_grant((void *)(uintptr_t)0x70010000u, 64u,
			     ULMK_TID_INVALID, ULMK_PERM_READ) != ULMK_OK);
}

/* ── heap ───────────────────────────────────────────────────────────────── */

static void test_heap_happy(void)
{
	uint32_t bits = 0u;

	progress("heap/happy");
	g_done = ulmk_notif_create();
	g_heap_ok = 0;
	g_heap_ext_ok = 0;
	(void)spawn("heap", heap_probe, NULL, 1u, 512u);
	ulmk_notif_wait(g_done, 0x1u, &bits);
	CHECK("get_heap", g_heap_ok);
	CHECK("extend", g_heap_ext_ok);
	ulmk_notif_destroy(g_done);
}

static void test_heap_edge(void)
{
	ulmk_heap_info_t hi;

	progress("heap/edge");
	/* Root typically has no heap — expect EPERM, not panic. */
	CHECK("root_heap",
	      ulmk_get_thread_heap(&hi) == ULMK_EPERM ||
	      ulmk_get_thread_heap(&hi) == ULMK_OK);
	CHECK("root_extend",
	      ulmk_heap_extend(64u) == ULMK_EPERM ||
	      ulmk_heap_extend(64u) == ULMK_OK ||
	      ulmk_heap_extend(64u) == ULMK_ENOMEM);
}

static void test_heap_crash(void)
{
	progress("heap/crash");
	CHECK("extend0", ulmk_heap_extend(0u) != ULMK_OK);
}

/* ── irq ────────────────────────────────────────────────────────────────── */

static void test_irq_happy(void)
{
	ulmk_notif_t n = ulmk_notif_create();

	progress("irq/happy");
	/* Single dynamic bind only — extra SRC slots Class-4 on TC275. */
	CHECK("bind", ulmk_irq_bind(5u, n, 0u) == ULMK_OK);
	CHECK("enable", ulmk_irq_enable(5u) == ULMK_OK);
	CHECK("ack", ulmk_irq_ack(5u) == ULMK_OK);
	CHECK("disable", ulmk_irq_disable(5u) == ULMK_OK);
	ulmk_notif_destroy(n);
}

static void test_irq_edge(void)
{
	ulmk_notif_t n = ulmk_notif_create();

	progress("irq/edge");
	CHECK("bind_hw0",
	      ulmk_irq_bind_hw(5u, n, 0u, 0u) == ULMK_EINVAL);
	/*
	 * Use a high SRPN never claimed by board drivers (tc275: 2–7,9
	 * are STM/ASCLIN/ADC/CAN/I2C/GPIO; 8 reserved by irq_stress).
	 */
	CHECK("en_unbound",
	      ulmk_irq_enable(200u) != ULMK_OK);
	CHECK("ack_unbound",
	      ulmk_irq_ack(200u) != ULMK_OK);
	ulmk_notif_destroy(n);
}

static void test_irq_crash(void)
{
	ulmk_notif_t n = ulmk_notif_create();

	progress("irq/crash");
	CHECK("bind0", ulmk_irq_bind(0u, n, 0u) != ULMK_OK);
	CHECK("bind_bit", ulmk_irq_bind(5u, n, 32u) != ULMK_OK);
	CHECK("en0", ulmk_irq_enable(0u) != ULMK_OK);
	CHECK("dis0", ulmk_irq_disable(0u) != ULMK_OK);
	CHECK("ack0", ulmk_irq_ack(0u) != ULMK_OK);
	CHECK("bind_badn",
	      ulmk_irq_bind(5u, ULMK_NOTIF_INVALID, 0u) != ULMK_OK);
	ulmk_notif_destroy(n);
}

/* ── cap ────────────────────────────────────────────────────────────────── */

static void test_cap_happy(void)
{
	progress("cap/happy");
	CHECK("grant", ulmk_cap_grant(g_target, ULMK_CAP_SPAWN) == ULMK_OK);
	CHECK("kill_tgt", ulmk_thread_kill(g_target) == ULMK_OK);
}

static void test_cap_crash(void)
{
	progress("cap/crash");
	CHECK("grant_dead",
	      ulmk_cap_grant(g_target, ULMK_CAP_SPAWN) != ULMK_OK);
}

void __attribute__((noinline)) silicon_unit_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_UNIT: begin\n");
	g_pass = 0;
	g_fail = 0;
	g_target = ULMK_TID_INVALID;

	test_thread_happy();
	test_thread_edge();
	test_thread_crash();

	test_ipc_happy();
	test_ipc_edge();
	test_ipc_crash();

	test_notif_happy();
	test_notif_edge();
	test_notif_crash();

	test_mem_happy();
	test_mem_edge();
	test_mem_crash();

	test_heap_happy();
	test_heap_edge();
	test_heap_crash();

	test_irq_happy();
	test_irq_edge();
	test_irq_crash();

	test_cap_happy();
	test_cap_crash();

	board_console_puts("SILICON_UNIT: REPORT\n");
	board_console_puts("pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_putc('\n');

	if (g_fail == 0) {
		ulmk_board_hil_mark(0x5017u);
		board_console_puts("SILICON_UNIT: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_UNIT: FAIL\n");
	}

	silicon_unit_done();
	ulmk_thread_exit();
}
