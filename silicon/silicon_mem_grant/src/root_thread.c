/* SPDX-License-Identifier: MIT */
/*
 * silicon_mem_grant — mem_grant peer read/write stability (HIL).
 *
 * Owner maps ANON, grants RW to a peer; peer reads magic, writes back;
 * owner observes the update.  Negatives: null/dead target, a thread with no
 * area touching the page, and the peer touching it again after revoke (its
 * window is still live in the MPU from the first access).
 *
 * HIL: SILICON_MEM_GRANT: PASS  scratch 0x6A17  silicon_mem_grant_done().
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

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_notif_t g_done;
/* Notifs hold one waiter: peer parks here while root waits on g_done. */
static ULMK_PRIVATE ulmk_notif_t g_rev;
static ULMK_PRIVATE volatile uint32_t *g_shared;
static ULMK_PRIVATE volatile int g_peer_read_ok;
static ULMK_PRIVATE volatile int g_peer_wrote;
static ULMK_PRIVATE volatile uint32_t g_peer_saw;
static ULMK_PRIVATE volatile int g_peer_after_revoke;
static ULMK_PRIVATE volatile int g_snoop_read;

#define MAGIC_OWNER	0xC0FFEEu
#define MAGIC_PEER	0xBEEFu

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
			uint8_t prio, uint32_t caps)
{
	ulmk_thread_attr_t a = {0};

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = 1024u;
	a.privilege  = ULMK_PRIV_DRIVER;
	a.caps       = caps;
	a.cpu = 0u;
	return ulmk_thread_create(&a);
}

static void peer_entry(void *arg)
{
	uint32_t bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_done, 0x1u, &bits);

	if (g_shared) {
		g_peer_saw = g_shared[0];
		g_peer_read_ok = (g_peer_saw == MAGIC_OWNER);
		g_shared[0] = MAGIC_PEER;
		g_peer_wrote = 1;
	}

	ulmk_notif_signal(g_done, 0x2u);

	bits = 0u;
	ulmk_notif_wait(g_rev, 0x8u, &bits);
	if (g_shared) {
		g_peer_saw = g_shared[0];
		g_peer_after_revoke = 1;
	}
	ulmk_notif_signal(g_rev, 0x10u);
	ulmk_thread_exit();
}

static void snoop_entry(void *arg)
{
	(void)arg;
	if (g_shared) {
		g_peer_saw = g_shared[0];
		g_snoop_read = 1;
	}
	ulmk_notif_signal(g_done, 0x4u);
}

/* Blocks root long enough for a faulting peer to run and be killed. */
static int killed_before(ulmk_notif_t n, ulmk_tid_t tid, uint32_t bit)
{
	uint32_t bits = 0u;
	int rc;

	rc = ulmk_notif_wait_timeout(n, bit, &bits, 50u);
	return rc == ULMK_ETIMEOUT &&
	       ulmk_thread_priority_get(tid) == ULMK_ESRCH;
}

static void idle_dead(void *arg)
{
	(void)arg;
	ulmk_thread_exit();
}

void __attribute__((noinline)) silicon_mem_grant_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t     *page;
	ulmk_tid_t    peer;
	ulmk_tid_t    dead;
	ulmk_tid_t    snoop;
	uint32_t      bits = 0u;
	int           rc;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_MEM_GRANT: begin\n");
	g_pass = 0;
	g_fail = 0;
	g_shared = NULL;
	g_peer_read_ok = 0;
	g_peer_wrote = 0;
	g_peer_saw = 0u;
	g_peer_after_revoke = 0;
	g_snoop_read = 0;

	g_done = ulmk_notif_create();
	g_rev = ulmk_notif_create();
	CHECK("notif", g_done != ULMK_NOTIF_INVALID &&
	      g_rev != ULMK_NOTIF_INVALID);

	progress("map");
	page = (uint32_t *)ulmk_mem_map(NULL, 256u,
					ULMK_PERM_READ | ULMK_PERM_WRITE,
					ULMK_MMAP_ANON);
	CHECK("map", map_ok(page));
	if (!map_ok(page))
		goto report;

	page[0] = MAGIC_OWNER;
	g_shared = page;

	progress("grant+peer");
	/* No inherited areas: the page must reach peer through the grant. */
	peer = spawn("peer", peer_entry, NULL, 10u, ULMK_CAP_NONE);
	CHECK("peer", peer != ULMK_TID_INVALID);

	rc = ulmk_mem_grant((void *)page, 256u, peer,
			    ULMK_PERM_READ | ULMK_PERM_WRITE);
	CHECK("grant", rc == ULMK_OK);

	ulmk_notif_signal(g_done, 0x1u);
	bits = 0u;
	ulmk_notif_wait(g_done, 0x2u, &bits);

	CHECK("peer_read", g_peer_read_ok);
	CHECK("peer_wrote", g_peer_wrote);
	CHECK("owner_sees", page[0] == MAGIC_PEER);

	progress("isolation");
	snoop = spawn("snoop", snoop_entry, NULL, 10u, ULMK_CAP_NONE);
	CHECK("snoop_killed", snoop != ULMK_TID_INVALID &&
	      killed_before(g_done, snoop, 0x4u) && !g_snoop_read);

	CHECK("revoke", ulmk_mem_revoke((void *)page, peer) == ULMK_OK);
	ulmk_notif_signal(g_rev, 0x8u);
	CHECK("revoked_killed", killed_before(g_rev, peer, 0x10u) &&
	      !g_peer_after_revoke);

	progress("neg");
	CHECK("grant_null",
	      ulmk_mem_grant(NULL, 256u, peer,
			     ULMK_PERM_READ) != ULMK_OK);

	dead = spawn("dead", idle_dead, NULL, 5u, ULMK_CAP_INHERIT);
	if (dead != ULMK_TID_INVALID) {
		CHECK("kill_dead", ulmk_thread_kill(dead) == ULMK_OK);
		CHECK("grant_dead",
		      ulmk_mem_grant((void *)page, 256u, dead,
				     ULMK_PERM_READ) != ULMK_OK);
	} else {
		CHECK("kill_dead", 0);
		CHECK("grant_dead", 0);
	}

	CHECK("unmap", ulmk_mem_unmap((void *)page, 256u) == ULMK_OK);

report:
	board_console_puts("SILICON_MEM_GRANT: REPORT\n");
	board_console_puts("pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_putc('\n');

	if (g_fail == 0) {
		ulmk_board_hil_mark(0x6A17u);
		board_console_puts("SILICON_MEM_GRANT: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_MEM_GRANT: FAIL\n");
	}

	silicon_mem_grant_done();
	ulmk_thread_exit();
}
