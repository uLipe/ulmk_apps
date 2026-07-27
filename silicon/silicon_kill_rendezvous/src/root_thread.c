/* SPDX-License-Identifier: MIT */
/*
 * silicon_kill_rendezvous — HIL.
 * Expect: SILICON_KILL_RENDEZVOUS: PASS  scratch 0x0103
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


void __attribute__((noinline)) silicon_kill_rendezvous_done(void)
{
}

#define BIT_GO		(1u << 0)
#define BIT_HOLDING	(1u << 1)
#define BIT_DONE	(1u << 2)
#define BIT_PARKED	(1u << 3)

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_ep_t g_ep;
static ULMK_PRIVATE ulmk_notif_t g_sync;
static ULMK_PRIVATE volatile int g_call_rc;
static ULMK_PRIVATE volatile int g_recv_rc;
static ULMK_PRIVATE ulmk_tid_t g_server;
static ULMK_PRIVATE ulmk_tid_t g_client;

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

/* --- A: server holds after recv; root kills; client unblocks --- */

static void srv_hold(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;
	uint32_t   bits = 0u;

	(void)arg;
	if (ulmk_ep_recv(g_ep, &m, &sender) != ULMK_OK)
		ulmk_thread_exit();
	ulmk_notif_signal(g_sync, BIT_HOLDING);
	ulmk_notif_wait(g_sync, BIT_PARKED, &bits); /* killed before wake */
	ulmk_thread_exit();
}

static void cli_call(void *arg)
{
	ulmk_msg_t m;
	uint32_t   bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	m.label = 0xAAu;
	g_call_rc = ulmk_ep_call(g_ep, &m);
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

/* --- B: client parked on send_queue; kill client; server recv empty --- */

static void cli_park_call(void *arg)
{
	ulmk_msg_t m;

	(void)arg;
	m.label = 0xBBu;
	(void)ulmk_ep_call(g_ep, &m);
	ulmk_thread_exit();
}

static void srv_empty_recv(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;
	uint32_t   bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	g_recv_rc = ulmk_ep_recv(g_ep, &m, &sender);
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

/* --- C: kill server on recv; new server + call works --- */

static void srv_park_recv(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;

	(void)arg;
	(void)ulmk_ep_recv(g_ep, &m, &sender);
	ulmk_thread_exit();
}

static void srv_ok(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;

	(void)arg;
	if (ulmk_ep_recv(g_ep, &m, &sender) != ULMK_OK)
		ulmk_thread_exit();
	m.label = 0xCCu;
	(void)ulmk_ep_reply(sender, &m);
	ulmk_thread_exit();
}

static void cli_ok(void *arg)
{
	ulmk_msg_t m;
	uint32_t   bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	m.label = 1u;
	g_call_rc = ulmk_ep_call(g_ep, &m);
	if (g_call_rc == ULMK_OK && m.label != 0xCCu)
		g_call_rc = ULMK_EINVAL;
	ulmk_notif_signal(g_sync, BIT_DONE);
	ulmk_thread_exit();
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t bits = 0u;
	int      i;
	int      rc;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);
	board_console_puts("SILICON_KILL_RENDEZVOUS: begin\n");
	g_pass = 0;
	g_fail = 0;

	g_sync = ulmk_notif_create();
	CHECK("sync", g_sync != ULMK_NOTIF_INVALID);

	/* ----- case A ----- */
	g_ep = ulmk_ep_create();
	g_call_rc = 0;
	CHECK("A_ep", g_ep != ULMK_EP_INVALID);
	g_server = spawn("srvA", srv_hold, NULL, 50u, 1024u, 0u);
	g_client = spawn("cliA", cli_call, NULL, 10u, 1024u, 0u);
	CHECK("A_spawn", g_server != ULMK_TID_INVALID &&
			 g_client != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, g_server);
	(void)ulmk_ep_grant(g_ep, g_client);

	(void)ulmk_thread_priority_set(ulmk_thread_self(), 200u);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();

	ulmk_notif_signal(g_sync, BIT_GO);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_HOLDING, &bits);
	rc = ulmk_thread_kill(g_server);
	CHECK("A_kill", rc == ULMK_OK);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("A_caller_esrch", g_call_rc == ULMK_ESRCH);
	(void)ulmk_ep_destroy(g_ep);

	/* ----- case B ----- */
	g_ep = ulmk_ep_create();
	g_recv_rc = ULMK_OK;
	CHECK("B_ep", g_ep != ULMK_EP_INVALID);
	g_client = spawn("cliB", cli_park_call, NULL, 10u, 1024u, 0u);
	g_server = spawn("srvB", srv_empty_recv, NULL, 50u, 1024u, 0u);
	CHECK("B_spawn", g_server != ULMK_TID_INVALID &&
			 g_client != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, g_server);
	(void)ulmk_ep_grant(g_ep, g_client);

	for (i = 0; i < 8; i++)
		ulmk_thread_yield();

	rc = ulmk_thread_kill(g_client);
	CHECK("B_kill", rc == ULMK_OK);
	ulmk_notif_signal(g_sync, BIT_GO);
	/*
	 * Server was waiting for GO then recv. With empty send_queue it
	 * blocks — wake it by destroying the ep (EINVAL), proving no ghost.
	 */
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();
	rc = ulmk_ep_destroy(g_ep);
	CHECK("B_destroy", rc == ULMK_OK);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("B_recv_einval", g_recv_rc == ULMK_EINVAL);

	/* ----- case C ----- */
	g_ep = ulmk_ep_create();
	g_call_rc = ULMK_EINVAL;
	CHECK("C_ep", g_ep != ULMK_EP_INVALID);
	g_server = spawn("srvC0", srv_park_recv, NULL, 50u, 1024u, 0u);
	CHECK("C_spawn0", g_server != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, g_server);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();
	CHECK("C_kill0", ulmk_thread_kill(g_server) == ULMK_OK);

	g_server = spawn("srvC1", srv_ok, NULL, 50u, 1024u, 0u);
	g_client = spawn("cliC", cli_ok, NULL, 10u, 1024u, 0u);
	CHECK("C_spawn1", g_server != ULMK_TID_INVALID &&
			  g_client != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, g_server);
	(void)ulmk_ep_grant(g_ep, g_client);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();
	ulmk_notif_signal(g_sync, BIT_GO);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_DONE, &bits);
	CHECK("C_call_ok", g_call_rc == ULMK_OK);
	(void)ulmk_ep_destroy(g_ep);

	board_console_puts("SILICON_KILL_RENDEZVOUS: REPORT pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_puts("\n");
	if (g_fail == 0) {
		ulmk_board_hil_mark(0x0103u);
		board_console_puts("SILICON_KILL_RENDEZVOUS: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_KILL_RENDEZVOUS: FAIL\n");
	}
	silicon_kill_rendezvous_done();
	ulmk_thread_exit();
}