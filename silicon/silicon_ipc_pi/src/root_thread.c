/* SPDX-License-Identifier: MIT */
/*
 * silicon_ipc_pi — priority inheritance on ep_call (HIL).
 * HIL: SILICON_IPC_PI: PASS  scratch 0x0101  silicon_ipc_pi_done().
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

#define PRIO_SERVER		100u
#define PRIO_CLIENT		10u
#define BIT_GO			(1u << 0)
#define BIT_CLIENT_DONE		(1u << 1)
#define BIT_SRV_HOLD		(1u << 2)

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_ep_t g_ep;
static ULMK_PRIVATE ulmk_notif_t g_sync;
static ULMK_PRIVATE volatile int g_prio_during;
static ULMK_PRIVATE volatile int g_call_ok;
static ULMK_PRIVATE ulmk_tid_t g_server;

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
	ulmk_thread_attr_t a = {0};

	a.name       = name;
	a.entry      = entry;
	a.arg        = NULL;
	a.priority   = prio;
	a.stack_size = 1024u;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.heap_size  = 0u;
	a.cpu = 0u;
	return ulmk_thread_create(&a);
}

static void server_entry(void *arg)
{
	ulmk_msg_t m;
	ulmk_tid_t sender;
	uint32_t   bits = 0u;

	(void)arg;
	if (ulmk_ep_recv(g_ep, &m, &sender) != ULMK_OK)
		ulmk_thread_exit();
	g_prio_during = ulmk_thread_priority_get(ulmk_thread_self());
	m.label = 0xA11u;
	(void)ulmk_ep_reply(sender, &m);
	ulmk_notif_wait(g_sync, BIT_SRV_HOLD, &bits);
	ulmk_thread_exit();
}

static void client_entry(void *arg)
{
	ulmk_msg_t m;
	uint32_t   bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	m.label = 0x42u;
	g_call_ok = (ulmk_ep_call(g_ep, &m) == ULMK_OK && m.label == 0xA11u);
	ulmk_notif_signal(g_sync, BIT_CLIENT_DONE);
	ulmk_thread_exit();
}

void __attribute__((noinline)) silicon_ipc_pi_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	ulmk_tid_t client;
	uint32_t   bits = 0u;
	int        i;
	int        prio;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_IPC_PI: begin\n");
	g_pass = 0;
	g_fail = 0;
	g_prio_during = -1;
	g_call_ok = 0;

	g_ep = ulmk_ep_create();
	g_sync = ulmk_notif_create();
	CHECK("ep", g_ep != ULMK_EP_INVALID);
	CHECK("notif", g_sync != ULMK_NOTIF_INVALID);

	g_server = spawn("srv", server_entry, PRIO_SERVER);
	client = spawn("cli", client_entry, PRIO_CLIENT);
	CHECK("spawn_srv", g_server != ULMK_TID_INVALID);
	CHECK("spawn_cli", client != ULMK_TID_INVALID);
	(void)ulmk_ep_grant(g_ep, g_server);
	(void)ulmk_ep_grant(g_ep, client);

	/*
	 * Root boots at prio 0; yield alone never runs lower-prio threads.
	 * Drop so client blocks on GO and server parks on ep_recv first.
	 */
	(void)ulmk_thread_priority_set(ulmk_thread_self(), 200u);
	for (i = 0; i < 8; i++)
		ulmk_thread_yield();

	ulmk_notif_signal(g_sync, BIT_GO);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_CLIENT_DONE, &bits);

	CHECK("call", g_call_ok);
	CHECK("boosted", g_prio_during == (int)PRIO_CLIENT);

	prio = ulmk_thread_priority_get(g_server);
	CHECK("restored", prio == (int)PRIO_SERVER);

	ulmk_notif_signal(g_sync, BIT_SRV_HOLD);

	board_console_puts("SILICON_IPC_PI: REPORT pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_puts(" prio_during=");
	put_u32((uint32_t)(g_prio_during < 0 ? 999u : (uint32_t)g_prio_during));
	board_console_putc('\n');

	if (g_fail == 0) {
		ulmk_board_hil_mark(0x0101u);
		board_console_puts("SILICON_IPC_PI: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_IPC_PI: FAIL\n");
	}
	silicon_ipc_pi_done();
	ulmk_thread_exit();
}
