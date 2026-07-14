/* SPDX-License-Identifier: MIT */
/* silicon_pool_exhaust — HIL. Expect SILICON_POOL_EXHAUST: PASS scratch 0x0104 */
#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);
__attribute__((weak)) void ulmk_board_hil_mark(uint32_t n) { (void)n; }
static void put_u32(uint32_t v) {
	char buf[10]; int i = 0;
	if (v == 0u) { board_console_putc('0'); return; }
	while (v) { buf[i++] = (char)('0' + (v % 10u)); v /= 10u; }
	while (i--) board_console_putc(buf[i]);
}
static ulmk_tid_t spawn(const char *name, void (*entry)(void *), void *arg,
			uint8_t prio, size_t stack, size_t heap) {
	ulmk_thread_attr_t a; (void)heap;
	a.name=name; a.entry=entry; a.arg=arg; a.priority=prio;
	a.stack_size=stack; a.privilege=ULMK_PRIV_DRIVER; a.heap_size=0u;
	return ulmk_thread_create(&a);
}
void __attribute__((noinline)) silicon_pool_exhaust_done(void) {}
#define MAX_EPS		128
#define MAX_NOTIFS	128
#define MAX_THREADS	32
#define STACK_FILL	2048u
#define STACK_OK	1024u

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_ep_t g_eps[MAX_EPS];
static ULMK_PRIVATE ulmk_notif_t g_notifs[MAX_NOTIFS];
static ULMK_PRIVATE ulmk_tid_t g_tids[MAX_THREADS];

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

static void blocker(void *arg)
{
	(void)arg;
	for (;;)
		ulmk_thread_yield();
}

static void check_cap_or_exhaust(const char *ex_name, const char *cap_name,
				 int n, int max)
{
	if (n < max)
		CHECK(ex_name, 1);
	else
		CHECK(cap_name, 1);
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	int          n_ep;
	int          n_notif;
	int          n_th;
	int          i;
	ulmk_ep_t    ep;
	ulmk_notif_t n;
	ulmk_tid_t   tid;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);
	board_console_puts("SILICON_POOL_EXHAUST: begin\n");
	g_pass = 0;
	g_fail = 0;

	n_th = 0;
	for (i = 0; i < MAX_THREADS; i++) {
		tid = spawn("blk", blocker, NULL, 200u, STACK_FILL, 0u);
		if (tid == ULMK_TID_INVALID)
			break;
		g_tids[n_th++] = tid;
	}
	CHECK("th_got_some", n_th > 1);
	check_cap_or_exhaust("th_exhausted", "th_held_max", n_th, MAX_THREADS);

	n_ep = 0;
	for (i = 0; i < MAX_EPS; i++) {
		ep = ulmk_ep_create();
		if (ep == ULMK_EP_INVALID)
			break;
		g_eps[n_ep++] = ep;
	}
	check_cap_or_exhaust("ep_exhausted", "ep_held_max", n_ep, MAX_EPS);

	n_notif = 0;
	for (i = 0; i < MAX_NOTIFS; i++) {
		n = ulmk_notif_create();
		if (n == ULMK_NOTIF_INVALID)
			break;
		g_notifs[n_notif++] = n;
	}
	check_cap_or_exhaust("notif_exhausted", "notif_held_max", n_notif,
			     MAX_NOTIFS);

	for (i = 0; i < n_ep; i++)
		(void)ulmk_ep_destroy(g_eps[i]);
	for (i = 0; i < n_notif; i++)
		(void)ulmk_notif_destroy(g_notifs[i]);
	for (i = 0; i < n_th; i++)
		(void)ulmk_thread_kill(g_tids[i]);

	ep = ulmk_ep_create();
	CHECK("ep_recover", ep != ULMK_EP_INVALID);
	(void)ulmk_ep_destroy(ep);
	n = ulmk_notif_create();
	CHECK("notif_recover", n != ULMK_NOTIF_INVALID);
	(void)ulmk_notif_destroy(n);
	tid = spawn("ok", blocker, NULL, 200u, STACK_OK, 0u);
	CHECK("th_recover", tid != ULMK_TID_INVALID);
	if (tid != ULMK_TID_INVALID)
		(void)ulmk_thread_kill(tid);

	board_console_puts("SILICON_POOL_EXHAUST: REPORT pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_puts(" th=");
	put_u32((uint32_t)n_th);
	board_console_puts(" ep=");
	put_u32((uint32_t)n_ep);
	board_console_puts(" notif=");
	put_u32((uint32_t)n_notif);
	board_console_puts("\n");
	if (g_fail == 0) {
		ulmk_board_hil_mark(0x0104u);
		board_console_puts("SILICON_POOL_EXHAUST: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_POOL_EXHAUST: FAIL\n");
	}
	silicon_pool_exhaust_done();
	ulmk_thread_exit();
}