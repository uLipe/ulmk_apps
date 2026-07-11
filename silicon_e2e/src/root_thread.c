/* SPDX-License-Identifier: MIT */
/*
 * silicon_e2e — public API smoke test on TC275 silicon (ulmk_apps).
 *
 * Mirrors tests/sdk_e2e/sdk_test.c: exercise public syscalls from the root
 * thread and print SILICON_E2E: PASS/FAIL to the board console (RAM log +
 * ASCLIN TX).  Validated on hardware via hil-silicon-e2e.sh.
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

ulmk_tid_t board_console_start(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);
void board_services_init(const ulmk_boot_info_t *info);

/*
 * Statics must live in the component domain (user RAM).  Plain .data/.bss
 * from a component archive lands in kernel SRAM and traps Class 1 under
 * PRS1 on TriCore silicon.
 */
static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;

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
	board_console_puts(ok ? "  [ok]   " : "  [FAIL] ");
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

static ULMK_PRIVATE ulmk_notif_t     g_done;
static ULMK_PRIVATE ulmk_ep_t        g_ipc_ep;
static ULMK_PRIVATE volatile int     g_heap_ok;
static ULMK_PRIVATE volatile int     g_heap_extend_ok;
static ULMK_PRIVATE ulmk_tid_t       g_target;

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
}

static void heap_probe(void *arg)
{
	ulmk_heap_info_t hi;

	(void)arg;
	g_heap_ok = (ulmk_get_thread_heap(&hi) == ULMK_OK && hi.size > 0u);
	g_heap_extend_ok = (ulmk_heap_extend(256) == ULMK_OK);
	ulmk_notif_signal(g_done, 0x1u);
}

static void idle_target(void *arg)
{
	(void)arg;
	for (;;)
		ulmk_thread_yield();
}

static ulmk_tid_t spawn(const char *name, void (*entry)(void *), void *arg,
			uint8_t prio, size_t heap)
{
	ulmk_thread_attr_t a;

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = 1024;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.heap_size  = heap;
	return ulmk_thread_create(&a);
}

static void test_thread_api(void)
{
	ulmk_tid_t self = ulmk_thread_self();

	CHECK("thread_self", self != ULMK_TID_INVALID);
	CHECK("thread_yield", ulmk_thread_yield() == ULMK_OK);

	g_target = spawn("target", idle_target, NULL, 200u, 0u);
	CHECK("thread_create", g_target != ULMK_TID_INVALID);

	CHECK("thread_priority_get(self)", ulmk_thread_priority_get(self) == 0);
	CHECK("thread_priority_get(target)",
	      ulmk_thread_priority_get(g_target) == 200);
	CHECK("thread_priority_set",
	      ulmk_thread_priority_set(g_target, 150) == ULMK_OK);
	CHECK("thread_priority_set verify",
	      ulmk_thread_priority_get(g_target) == 150);
	CHECK("thread_suspend", ulmk_thread_suspend(g_target) == ULMK_OK);
	CHECK("thread_resume", ulmk_thread_resume(g_target) == ULMK_OK);
}

static void test_ipc(void)
{
	ulmk_ep_t  ep;
	ulmk_ep_t  tmp;
	ulmk_tid_t srv;
	ulmk_msg_t m;
	int        rc;

	ep = ulmk_ep_create();
	g_ipc_ep = ep;
	CHECK("ep_create", ep != ULMK_EP_INVALID);

	srv = spawn("ipc_srv", ipc_server, NULL, 1u, 0u);
	CHECK("ep_grant", ulmk_ep_grant(ep, srv) == ULMK_OK);

	m.label = 0x100u;
	rc = ulmk_ep_call(ep, &m);
	CHECK("ep_call + ep_recv + ep_reply", rc == ULMK_OK && m.label == 0x101u);

	m.label = 0x200u;
	rc = ulmk_ep_call(ep, &m);
	CHECK("ep_reply_recv", rc == ULMK_OK && m.label == 0x201u);

	tmp = ulmk_ep_create();
	CHECK("ep_destroy", ulmk_ep_destroy(tmp) == ULMK_OK);
}

static void test_recv_or_notif(void)
{
	ulmk_ep_t    ep;
	ulmk_notif_t n;
	ulmk_msg_t   msg;
	ulmk_tid_t   sender;
	uint32_t     bits = 0u;
	int          rc;

	ep = ulmk_ep_create();
	n  = ulmk_notif_create();
	ulmk_notif_signal(n, 0x8u);

	rc = ulmk_ep_recv_or_notif(ep, n, 0x8u, &msg, &sender, &bits);
	CHECK("ep_recv_or_notif", rc == 1 && bits == 0x8u);

	ulmk_notif_destroy(n);
	ulmk_ep_destroy(ep);
}

static void test_notif(void)
{
	ulmk_notif_t n;
	uint32_t     bits = 0u;

	n = ulmk_notif_create();
	CHECK("notif_create", n != ULMK_NOTIF_INVALID);
	CHECK("notif_signal", ulmk_notif_signal(n, 0x1u) == ULMK_OK);
	CHECK("notif_poll", ulmk_notif_poll(n, 0x1u) == 0x1u);
	CHECK("notif_signal (2)", ulmk_notif_signal(n, 0x2u) == ULMK_OK);
	CHECK("notif_wait",
	      ulmk_notif_wait(n, 0x2u, &bits) == ULMK_OK && bits == 0x2u);
	CHECK("notif_destroy", ulmk_notif_destroy(n) == ULMK_OK);
}

static void test_memory(void)
{
	volatile uint32_t *p;

	p = (volatile uint32_t *)ulmk_mem_map(
		NULL, 256u, ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_ANON);
	CHECK("mem_map (anon)", map_ok((const void *)p));

	if (map_ok((const void *)p)) {
		p[0] = 0xdeadbeefu;
		CHECK("mem_map read/write", p[0] == 0xdeadbeefu);
		CHECK("mem_grant",
		      ulmk_mem_grant((void *)p, 256u, g_target,
				     ULMK_PERM_READ | ULMK_PERM_WRITE) == ULMK_OK);
		CHECK("mem_unmap", ulmk_mem_unmap((void *)p, 256u) == ULMK_OK);
	} else {
		check("mem_map read/write", 0);
		check("mem_grant", 0);
		check("mem_unmap", 0);
	}
}

static void test_heap(void)
{
	uint32_t bits = 0u;

	g_done = ulmk_notif_create();
	spawn("heap", heap_probe, NULL, 1u, 512u);
	ulmk_notif_wait(g_done, 0x1u, &bits);

	CHECK("get_thread_heap", g_heap_ok);
	CHECK("heap_extend", g_heap_extend_ok);

	ulmk_notif_destroy(g_done);
}

static void test_irq(void)
{
	ulmk_notif_t n = ulmk_notif_create();

	CHECK("irq_bind", ulmk_irq_bind(5u, n, 0u) == ULMK_OK);
	CHECK("irq_enable", ulmk_irq_enable(5u) == ULMK_OK);
	CHECK("irq_ack", ulmk_irq_ack(5u) == ULMK_OK);
	CHECK("irq_disable", ulmk_irq_disable(5u) == ULMK_OK);
	CHECK("irq_bind_hw (reject)",
	      ulmk_irq_bind_hw(5u, n, 0u, 0u) == ULMK_EINVAL);

	ulmk_notif_destroy(n);
}

static void test_capabilities(void)
{
	CHECK("cap_grant", ulmk_cap_grant(g_target, ULMK_CAP_SPAWN) == ULMK_OK);
	CHECK("thread_kill", ulmk_thread_kill(g_target) == ULMK_OK);
}

static void __attribute__((noinline)) silicon_e2e_done(void)
{
	/* HIL breakpoint anchor — do not inline. */
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_E2E: begin\n");

	board_console_puts("> thread\n");
	test_thread_api();
	board_console_puts("> ipc\n");
	test_ipc();
	board_console_puts("> recv_or_notif\n");
	test_recv_or_notif();
	board_console_puts("> notif\n");
	test_notif();
	board_console_puts("> memory\n");
	test_memory();
	board_console_puts("> heap\n");
	test_heap();
	board_console_puts("> irq\n");
	test_irq();
	board_console_puts("> caps\n");
	test_capabilities();

	board_console_puts("SILICON_E2E: ");
	put_u32((uint32_t)g_pass);
	board_console_puts("/");
	put_u32((uint32_t)(g_pass + g_fail));
	board_console_puts(" checks passed\n");

	if (g_fail == 0) {
		ulmk_board_hil_mark(0xE2EAu);
		board_console_puts("SILICON_E2E: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_E2E: FAIL\n");
	}

	silicon_e2e_done();
	ulmk_thread_exit();
}
