/* SPDX-License-Identifier: MIT */
/*
 * silicon_mem_grant — mem_grant peer read/write stability (HIL).
 *
 * Owner maps ANON, grants RW to a peer; peer reads magic, writes back;
 * owner observes the update.  Negatives: null/dead target, a thread with no
 * area touching the page, and the peer touching it again after revoke (its
 * window is still live in the MPU from the first access).  Revoke cascades
 * to a re-grant, free takes an inherited block from the child, and a TCB
 * handle is no door into the pool.
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
static ULMK_PRIVATE ulmk_notif_t g_park;
static ULMK_PRIVATE volatile ulmk_tid_t g_gc;
static ULMK_PRIVATE volatile int g_mid_ok;
static ULMK_PRIVATE volatile uint32_t *g_probe;
static ULMK_PRIVATE volatile int g_probe_ok;
static ULMK_PRIVATE volatile int g_probe_alive;

#define MAGIC_OWNER	0xC0FFEEu
#define MAGIC_PEER	0xBEEFu

#define P_GO		0x1u
#define P_READ		0x2u
#define P_GO2		0x4u
#define P_ALIVE		0x8u

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

/*
 * Reads *g_probe once while it should have access, then again after the
 * owner took it away; each probe has its own notif, passed as @arg.
 */
static void probe_entry(void *arg)
{
	ulmk_notif_t n = (ulmk_notif_t)arg;
	uint32_t     bits = 0u;

	ulmk_notif_wait(n, P_GO, &bits);
	g_probe_ok = (*g_probe == MAGIC_OWNER);
	ulmk_notif_signal(n, P_READ);

	bits = 0u;
	ulmk_notif_wait(n, P_GO2, &bits);
	(void)*g_probe;
	g_probe_alive = 1;
	ulmk_notif_signal(n, P_ALIVE);
	ulmk_thread_exit();
}

static int probe_reads(ulmk_notif_t n)
{
	uint32_t bits = 0u;

	g_probe_ok = 0;
	ulmk_notif_signal(n, P_GO);
	ulmk_notif_wait(n, P_READ, &bits);
	return g_probe_ok;
}

static int probe_killed(ulmk_notif_t n, ulmk_tid_t tid)
{
	g_probe_alive = 0;
	ulmk_notif_signal(n, P_GO2);
	return killed_before(n, tid, P_ALIVE) && !g_probe_alive;
}

/* Re-grants the page to g_gc, then parks so its own area stays alive. */
static void mid_entry(void *arg)
{
	uint32_t bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_park, 0x2u, &bits);
	g_mid_ok = (ulmk_mem_grant((void *)g_shared, 256u, g_gc,
				   ULMK_PERM_READ) == ULMK_OK);
	ulmk_notif_signal(g_done, 0x40u);
	bits = 0u;
	ulmk_notif_wait(g_park, 0x1u, &bits);
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
	ulmk_tid_t    mid;
	ulmk_tid_t    child;
	ulmk_tid_t    tcb;
	ulmk_notif_t  gn;
	ulmk_notif_t  hn;
	ulmk_notif_t  tn;
	uint32_t     *blk;
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

	progress("cascade");
	g_park = ulmk_notif_create();
	gn = ulmk_notif_create();
	g_probe = page;
	page[0] = MAGIC_OWNER;
	g_gc = spawn("gc", probe_entry, (void *)gn, 10u, ULMK_CAP_NONE);
	mid = spawn("mid", mid_entry, NULL, 10u, ULMK_CAP_NONE);
	CHECK("cascade_spawn", g_park != ULMK_NOTIF_INVALID &&
	      gn != ULMK_NOTIF_INVALID && g_gc != ULMK_TID_INVALID &&
	      mid != ULMK_TID_INVALID);
	CHECK("mid_grant", ulmk_mem_grant((void *)page, 256u, mid,
					  ULMK_PERM_READ) == ULMK_OK);
	ulmk_notif_signal(g_park, 0x2u);
	bits = 0u;
	ulmk_notif_wait(g_done, 0x40u, &bits);
	CHECK("mid_regrant", g_mid_ok);
	CHECK("gc_read", probe_reads(gn));
	CHECK("revoke_mid", ulmk_mem_revoke((void *)page, mid) == ULMK_OK);
	CHECK("revoke_cascade",
	      ulmk_mem_revoke((void *)page, g_gc) != ULMK_OK);
	CHECK("gc_killed", probe_killed(gn, g_gc));
	CHECK("kill_mid", ulmk_thread_kill(mid) == ULMK_OK);

	progress("inherit");
	hn = ulmk_notif_create();
	blk = (uint32_t *)ulmk_malloc(64u);
	CHECK("malloc", hn != ULMK_NOTIF_INVALID && blk != NULL);
	if (blk) {
		blk[0] = MAGIC_OWNER;
		g_probe = blk;
		child = spawn("child", probe_entry, (void *)hn, 10u,
			      ULMK_CAP_INHERIT);
		CHECK("child_read", child != ULMK_TID_INVALID &&
		      probe_reads(hn));
		ulmk_free(blk);
		CHECK("free_killed", probe_killed(hn, child));
	}

	progress("tcb");
	tn = ulmk_notif_create();
	g_probe = (volatile uint32_t *)ulmk_thread_self();
	tcb = spawn("tcb", probe_entry, (void *)tn, 10u, ULMK_CAP_INHERIT);
	ulmk_notif_signal(tn, P_GO);
	CHECK("tcb_killed", tcb != ULMK_TID_INVALID &&
	      killed_before(tn, tcb, P_READ));

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
