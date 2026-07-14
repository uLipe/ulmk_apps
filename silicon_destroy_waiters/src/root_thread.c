/* SPDX-License-Identifier: MIT */
/*
 * silicon_destroy_waiters — ep/notif destroy unblocks waiters (HIL).
 * HIL: SILICON_DESTROY_WAITERS: PASS  scratch 0x0102
 *      silicon_destroy_waiters_done().
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);

__attribute__((weak)) void ulmk_board_hil_mark(uint32_t n)
{
	(void)n;
}

#define BIT_DONE	(1u << 0)

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_ep_t g_ep;
static ULMK_PRIVATE ulmk_notif_t g_sync;
static ULMK_PRIVATE ulmk_notif_t g_target;
static ULMK_PRIVATE volatile int g_recv_rc;
static ULMK_PRIVATE volatile int g_call_rc;
static ULMK_PRIVATE volatile int g_wait_rc;

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
	board_console_putc('\n');
	if (ok)
		g_pass++;
	else
		g_fail++;
}

#define CHECK(name, cond) check((name), (cond) ? 1 : 0)

static ulmk_tid_t spawn(const char *name, void (*entry)(void *), uint8_t prio)
{
	ulmk_thread_attr_t a;

	a.name       = name;
	a.entry      = entry;
	a.arg        = NULL;
	a.priority   = prio;
	a.stack_size = 1024u;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.heap_size  = 0u;
	return ulmk_thread_create(&a);
}

static void recv_waiter(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;

	(void)arg;
	g_recv_rc = ulmk_ep_recv(g_ep, &m, &sender);
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

static void call_waiter(void *arg)
{
	ulmk_msg_t m;

	(void)arg;
	m.label = 1u;
	g_call_rc = ulmk_ep_call(g_ep, &m);
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

static void notif_waiter(void *arg)
{
	uint32_t bits = 0u;

	(void)arg;
	g_wait_rc = ulmk_notif_wait(g_target, 0x1u, &bits);
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

void __attribute__((noinline)) silicon_destroy_waiters_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t tid;
	uint32_t   bits = 0u;
	int        i;
	int        rc;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_DESTROY_WAITERS: begin\n");
	g_pass = 0;
	g_fail = 0;
	g_recv_rc = 0;
	g_call_rc = 0;
	g_wait_rc = 0;

	g_sync = ulmk_notif_create();
	CHECK("sync", g_sync != ULMK_NOTIF_INVALID);

	g_ep = ulmk_ep_create();
	CHECK("ep_recv_create", g_ep != ULMK_EP_INVALID);
	tid = spawn("recv_w", recv_waiter, 10u);
	CHECK("spawn_recv", tid != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, tid);

	(void)ulmk_thread_priority_set(ulmk_thread_self(), 200u);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();

	rc = ulmk_ep_destroy(g_ep);
	CHECK("ep_destroy_recv", rc == ULMK_OK);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("recv_einval", g_recv_rc == ULMK_EINVAL);

	g_ep = ulmk_ep_create();
	CHECK("ep_call_create", g_ep != ULMK_EP_INVALID);
	tid = spawn("call_w", call_waiter, 10u);
	CHECK("spawn_call", tid != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, tid);

	for (i = 0; i < 8; i++)
		ulmk_thread_yield();

	rc = ulmk_ep_destroy(g_ep);
	CHECK("ep_destroy_call", rc == ULMK_OK);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("call_einval", g_call_rc == ULMK_EINVAL);

	g_target = ulmk_notif_create();
	CHECK("notif_create", g_target != ULMK_NOTIF_INVALID);
	tid = spawn("notif_w", notif_waiter, 10u);
	CHECK("spawn_notif", tid != ULMK_TID_INVALID);

	for (i = 0; i < 8; i++)
		ulmk_thread_yield();

	rc = ulmk_notif_destroy(g_target);
	CHECK("notif_destroy", rc == ULMK_OK);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("wait_einval", g_wait_rc == ULMK_EINVAL);

	board_console_puts("SILICON_DESTROY_WAITERS: REPORT pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_putc('\n');

	if (g_fail == 0) {
		ulmk_board_hil_mark(0x0102u);
		board_console_puts("SILICON_DESTROY_WAITERS: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_DESTROY_WAITERS: FAIL\n");
	}
	silicon_destroy_waiters_done();
	ulmk_thread_exit();
}
