/* SPDX-License-Identifier: MIT */
/*
 * silicon_recv_or_notif_race — HIL.
 * Expect: SILICON_RECV_OR_NOTIF_RACE: PASS  scratch 0x0105
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

static ulmk_tid_t spawn(const char *name, void (*entry)(void *), void *arg,
			uint8_t prio, size_t stack, size_t heap)
{
	ulmk_thread_attr_t a = {0};

	(void)heap;
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


void __attribute__((noinline)) silicon_recv_or_notif_race_done(void)
{
}

#define BIT_GO		(1u << 0)
#define BIT_DONE	(1u << 1)
#define BIT_EVT		(1u << 0)

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_ep_t g_ep;
static ULMK_PRIVATE ulmk_notif_t g_evt;
static ULMK_PRIVATE ulmk_notif_t g_sync;
static ULMK_PRIVATE volatile int g_rc;
static ULMK_PRIVATE volatile uint32_t g_bits;
static ULMK_PRIVATE volatile ulmk_tid_t g_sender;
static ULMK_PRIVATE volatile uint32_t g_label;

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

static void server_ron(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;
	uint32_t   bits = 0u;
	int        rc;

	(void)arg;
	rc = ulmk_ep_recv_or_notif(g_ep, g_evt, BIT_EVT, &m, &sender, &bits);
	g_rc = rc;
	g_bits = bits;
	g_sender = sender;
	g_label = m.label;
	if (rc == ULMK_OK && sender != ULMK_TID_INVALID) {
		m.label = 0x90u;
		(void)ulmk_ep_reply(sender, &m);
	}
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

static void client_call(void *arg)
{
	ulmk_msg_t m;
	uint32_t   bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	m.label = 0x42u;
	(void)ulmk_ep_call(g_ep, &m);
	ulmk_thread_exit();
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t srv;
	ulmk_tid_t cli;
	uint32_t   bits = 0u;
	int        i;
	ulmk_msg_t m;
	ulmk_tid_t sender;
	uint32_t   nb = 0u;
	int        rc;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);
	board_console_puts("SILICON_RECV_OR_NOTIF_RACE: begin\n");
	g_pass = 0;
	g_fail = 0;

	g_sync = ulmk_notif_create();
	CHECK("sync", g_sync != ULMK_NOTIF_INVALID);

	/* --- notif-fast path (bits already set) --- */
	g_ep = ulmk_ep_create();
	g_evt = ulmk_notif_create();
	CHECK("nf_objs", g_ep != ULMK_EP_INVALID &&
			 g_evt != ULMK_NOTIF_INVALID);
	(void)ulmk_notif_signal(g_evt, BIT_EVT);
	rc = ulmk_ep_recv_or_notif(g_ep, g_evt, BIT_EVT, &m, &sender, &nb);
	CHECK("notif_fast", rc == 1 && nb == BIT_EVT &&
			    sender == ULMK_TID_INVALID);
	(void)ulmk_notif_destroy(g_evt);
	(void)ulmk_ep_destroy(g_ep);

	/* --- IPC path: client call wakes blocked server --- */
	g_ep = ulmk_ep_create();
	g_evt = ulmk_notif_create();
	g_rc = -99;
	g_bits = 0;
	g_sender = ULMK_TID_INVALID;
	CHECK("ipc_objs", g_ep != ULMK_EP_INVALID &&
			  g_evt != ULMK_NOTIF_INVALID);
	srv = spawn("srv", server_ron, NULL, 50u, 1024u, 0u);
	cli = spawn("cli", client_call, NULL, 10u, 1024u, 0u);
	CHECK("ipc_spawn", srv != ULMK_TID_INVALID &&
			   cli != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, srv);
	(void)ulmk_ep_grant(g_ep, cli);
	(void)ulmk_thread_priority_set(ulmk_thread_self(), 200u);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();
	ulmk_notif_signal(g_sync, BIT_GO);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("ipc_path", g_rc == ULMK_OK && g_sender != ULMK_TID_INVALID &&
			  g_label == 0x42u);
	(void)ulmk_ep_destroy(g_ep);
	(void)ulmk_notif_destroy(g_evt);

	/* --- race: signal + call while server blocked; either path OK --- */
	g_ep = ulmk_ep_create();
	g_evt = ulmk_notif_create();
	g_rc = -99;
	g_bits = 0;
	g_sender = ULMK_TID_INVALID;
	CHECK("race_objs", g_ep != ULMK_EP_INVALID &&
			   g_evt != ULMK_NOTIF_INVALID);
	srv = spawn("srvR", server_ron, NULL, 50u, 1024u, 0u);
	cli = spawn("cliR", client_call, NULL, 10u, 1024u, 0u);
	CHECK("race_spawn", srv != ULMK_TID_INVALID &&
			    cli != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, srv);
	(void)ulmk_ep_grant(g_ep, cli);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();
	ulmk_notif_signal(g_sync, BIT_GO);
	ulmk_notif_signal(g_evt, BIT_EVT);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("race_clean",
	      (g_rc == 1 && g_bits == BIT_EVT) ||
	      (g_rc == ULMK_OK && g_sender != ULMK_TID_INVALID));
	(void)ulmk_ep_destroy(g_ep);
	(void)ulmk_notif_destroy(g_evt);

	board_console_puts("SILICON_RECV_OR_NOTIF_RACE: REPORT pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_puts("\n");
	if (g_fail == 0) {
		ulmk_board_hil_mark(0x0105u);
		board_console_puts("SILICON_RECV_OR_NOTIF_RACE: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_RECV_OR_NOTIF_RACE: FAIL\n");
	}
	silicon_recv_or_notif_race_done();
	ulmk_thread_exit();
}