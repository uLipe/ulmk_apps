/* SPDX-License-Identifier: MIT */
/*
 * silicon_wcet — public syscall WCET / O(1) audit on silicon (ulmk_apps).
 *
 * Requires ULMK_CONFIG_SYSCALL_WCET=1.  Each sample is the kernel-side
 * pure CCNT delta (wall minus blocked RTT) around ulmk_kern_trap_syscall.
 * o1=1 iff min/max stay within ±10% of avg (2-cycle floor).
 * mem_map also checked across sizes 64/256/1024.
 *
 * Rows print min/avg/max for pure delta; blocking syscalls also print
 * blk=avg_blocked when the blocked bucket is non-zero.
 *
 * Peers that must run once use equal prio, set a flag, then park on a notif
 * (never yield-spin at prio 0 — that starves the console ep_call server).
 * Create/kill targets use low priority so they need not run.  thread_exit is
 * skip=noreturn.
 *
 * HIL: SILICON_WCET: PASS  scratch 0x5CE7  silicon_wcet_done().
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>
#include <ulmk/syscall_wcet.h>
#include <board_config.h>

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);

#define WCET_SAMPLES		8u
#define WCET_WARMUP		1u
#define WCET_TOL_NUM		10u
#define WCET_TOL_DEN		100u
#define WCET_FLOOR_TICKS	2u

static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_tid_t g_target;
static ULMK_PRIVATE ulmk_ep_t g_ipc_ep;
static ULMK_PRIVATE ulmk_notif_t g_park;
static ULMK_PRIVATE volatile int g_peer_ready;
static ULMK_PRIVATE volatile int g_peer_done;
static ULMK_PRIVATE ulmk_notif_t g_done;
static ULMK_PRIVATE uint32_t g_srv_recv[WCET_SAMPLES];
static ULMK_PRIVATE uint32_t g_srv_reply[WCET_SAMPLES];
static ULMK_PRIVATE uint32_t g_srv_rr[WCET_SAMPLES];
static ULMK_PRIVATE uint32_t g_srv_n;
static ULMK_PRIVATE uint32_t g_srv_rr_n;

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

static int o1_ok(uint32_t mn, uint32_t avg, uint32_t mx)
{
	uint32_t slack;

	if (avg == 0u)
		return (mx <= WCET_FLOOR_TICKS) ? 1 : 0;
	slack = (avg * WCET_TOL_NUM) / WCET_TOL_DEN;
	if (slack < WCET_FLOOR_TICKS)
		slack = WCET_FLOOR_TICKS;
	if (mx > avg + slack)
		return 0;
	if (mn + slack < avg)
		return 0;
	return 1;
}

static void emit_row(const char *name, uint32_t mn, uint32_t avg, uint32_t mx,
		     uint32_t blk_avg, int ok)
{
	board_console_puts(name);
	board_console_putc(' ');
	put_u32(mn);
	board_console_putc('/');
	put_u32(avg);
	board_console_putc('/');
	put_u32(mx);
	if (blk_avg != 0u) {
		board_console_puts(" blk=");
		put_u32(blk_avg);
	}
	board_console_puts(" o1=");
	board_console_putc(ok ? '1' : '0');
	board_console_putc('\n');
	if (!ok)
		g_fail++;
}

static void record(const char *name, uint32_t mn, uint32_t avg, uint32_t mx)
{
	emit_row(name, mn, avg, mx, 0u, o1_ok(mn, avg, mx));
}

static void record_blk(const char *name, uint32_t mn, uint32_t avg, uint32_t mx,
		       uint32_t blk_avg)
{
	emit_row(name, mn, avg, mx, blk_avg, o1_ok(mn, avg, mx));
}

static void record_custom(const char *name, uint32_t mn, uint32_t avg,
			  uint32_t mx, int ok)
{
	emit_row(name, mn, avg, mx, 0u, ok ? 1 : 0);
}

static void stats_from_samples(const uint32_t *s, uint32_t n,
			       uint32_t *mn, uint32_t *avg, uint32_t *mx)
{
	uint32_t i;
	uint64_t sum;

	*mn = 0xFFFFFFFFu;
	*mx = 0u;
	sum = 0u;
	for (i = 0u; i < n; i++) {
		if (s[i] < *mn)
			*mn = s[i];
		if (s[i] > *mx)
			*mx = s[i];
		sum += s[i];
	}
	*avg = (n > 0u) ? (uint32_t)(sum / n) : 0u;
}

static uint32_t slot_delta_after(uint32_t seq_before)
{
	if (g_ulmk_syscall_wcet.magic != ULMK_SYSCALL_WCET_MAGIC)
		return 0u;
	if (g_ulmk_syscall_wcet.seq == seq_before)
		return 0u;
	return g_ulmk_syscall_wcet.delta;
}

static uint32_t slot_blocked_after(uint32_t seq_before)
{
	if (g_ulmk_syscall_wcet.magic != ULMK_SYSCALL_WCET_MAGIC)
		return 0u;
	if (g_ulmk_syscall_wcet.seq == seq_before)
		return 0u;
	return g_ulmk_syscall_wcet.blocked;
}

static void sample_void_fn(void (*fn)(void), const char *name)
{
	uint32_t samples[WCET_SAMPLES];
	uint32_t blocked[WCET_SAMPLES];
	uint32_t i, seq, mn, avg, mx, bmn, bavg, bmx;

	for (i = 0u; i < WCET_WARMUP; i++)
		fn();
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		fn();
		samples[i] = slot_delta_after(seq);
		blocked[i] = slot_blocked_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	stats_from_samples(blocked, WCET_SAMPLES, &bmn, &bavg, &bmx);
	(void)bmn;
	(void)bmx;
	if (bavg != 0u)
		record_blk(name, mn, avg, mx, bavg);
	else
		record(name, mn, avg, mx);
}

static void wrap_self(void)
{
	(void)ulmk_thread_self();
}

static void wrap_yield(void)
{
	(void)ulmk_thread_yield();
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

/*
 * Equal-prio with root so yield can schedule us once; then park so we are
 * not runnable and cannot starve the board console server (prio 1).
 */
static void park_peer(void *arg)
{
	uint32_t bits;

	(void)arg;
	g_peer_ready = 1;
	(void)ulmk_notif_wait(g_park, 0x1u, &bits);
	ulmk_thread_exit();
}

static void low_idle(void *arg)
{
	(void)arg;
	for (;;)
		ulmk_thread_yield();
}

static void ipc_server(void *arg)
{
	ulmk_ep_t  ep = g_ipc_ep;
	ulmk_msg_t m;
	ulmk_tid_t snd;
	uint32_t   seq, recv_dt, n = 0u;
	int        done;
	int        skip = 1;

	(void)arg;
	for (;;) {
		seq = g_ulmk_syscall_wcet.seq;
		if (ulmk_ep_recv(ep, &m, &snd) != ULMK_OK)
			break;
		recv_dt = slot_delta_after(seq);

		done = (m.label == 0xDEADu);
		if (done) {
			g_srv_n = (n > WCET_SAMPLES) ? WCET_SAMPLES : n;
			g_peer_done = 1;
			ulmk_ep_reply(snd, &m);
			break;
		}

		if (skip) {
			skip = 0;
			m.label++;
			ulmk_ep_reply(snd, &m);
			continue;
		}

		if (n < WCET_SAMPLES)
			g_srv_recv[n] = recv_dt;
		m.label++;
		seq = g_ulmk_syscall_wcet.seq;
		ulmk_ep_reply(snd, &m);
		if (n < WCET_SAMPLES)
			g_srv_reply[n] = slot_delta_after(seq);
		n++;
	}
	ulmk_thread_exit();
}

/* First recv, then reply_recv loop; samples are reply_recv deltas. */
static void ipc_rr_server(void *arg)
{
	ulmk_ep_t  ep = g_ipc_ep;
	ulmk_msg_t m;
	ulmk_msg_t out;
	ulmk_tid_t snd;
	uint32_t   seq;
	uint32_t   nr = 0u;

	(void)arg;
	g_peer_ready = 1;
	g_srv_rr_n = 0u;

	if (ulmk_ep_recv(ep, &m, &snd) != ULMK_OK)
		goto done;
	if (m.label == 0xDEADu) {
		ulmk_ep_reply(snd, &m);
		goto done;
	}

	for (;;) {
		out = m;
		out.label++;
		seq = g_ulmk_syscall_wcet.seq;
		if (ulmk_ep_reply_recv(ep, snd, &out, &m, &snd) != ULMK_OK)
			break;
		if (nr < WCET_SAMPLES)
			g_srv_rr[nr++] = slot_delta_after(seq);
		if (m.label == 0xDEADu) {
			ulmk_ep_reply(snd, &m);
			break;
		}
	}
	g_srv_rr_n = nr;
done:
	g_peer_done = 1;
	ulmk_thread_exit();
}

static ULMK_PRIVATE uint32_t g_heap_ext[WCET_SAMPLES];
static ULMK_PRIVATE volatile uint32_t g_heap_ext_n;

static void heap_extend_one(void *arg)
{
	uint32_t idx = (uint32_t)(uintptr_t)arg;
	uint32_t seq;

	seq = g_ulmk_syscall_wcet.seq;
	(void)ulmk_heap_extend(64u);
	if (idx < WCET_SAMPLES)
		g_heap_ext[idx] = slot_delta_after(seq);
	g_heap_ext_n++;
	ulmk_thread_exit();
}

static void heap_worker(void *arg)
{
	ulmk_heap_info_t hi;
	uint32_t samples[WCET_SAMPLES];
	uint32_t i, seq, mn, avg, mx;

	(void)arg;
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_get_thread_heap(&hi);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("get_thread_heap", mn, avg, mx);
	ulmk_notif_signal(g_done, 0x1u);
	ulmk_thread_exit();
}

static void wait_ready(void)
{
	uint32_t i;

	for (i = 0u; i < 4000u && !g_peer_ready; i++)
		ulmk_thread_yield();
	if (!g_peer_ready)
		g_fail++;
}

void __attribute__((noinline)) silicon_wcet_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t samples[WCET_SAMPLES];
	uint32_t i, seq, mn, avg, mx, bits, slack, s;
	uint32_t avgs[3];
	ulmk_tid_t self, peer, srv;
	ulmk_ep_t ep, tmp;
	ulmk_notif_t n;
	ulmk_msg_t m;
	ulmk_tid_t sender;
	void *p;
	size_t sizes[3];
	int ok;

	/*
	 * Assign on the stack — a const aggregate init can emit a memcpy from
	 * a .rodata address that the component link relocates into kernel RAM
	 * (Class 1 trap under PRS1 on TriCore silicon).
	 */
	sizes[0] = 64u;
	sizes[1] = 256u;
	sizes[2] = 1024u;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);
	board_console_puts("SILICON_WCET: begin\n");
	if (g_ulmk_syscall_wcet.magic != ULMK_SYSCALL_WCET_MAGIC) {
		board_console_puts("SILICON_WCET: FAIL no-slot\n");
		ulmk_board_hil_mark(0xDEADu);
		silicon_wcet_done();
		ulmk_thread_exit();
	}
	board_console_puts("SILICON_WCET: REPORT\n");
	board_console_puts("unit=cpu_cycles slot=kern_pure\n");
	board_console_puts("n=");
	put_u32(WCET_SAMPLES);
	board_console_puts(" tol%=");
	put_u32(WCET_TOL_NUM);
	board_console_putc('\n');

	g_fail = 0;
	g_target = ULMK_TID_INVALID;
	g_park = ulmk_notif_create();

	board_console_puts("> thread\n");
	sample_void_fn(wrap_self, "thread_self");
	sample_void_fn(wrap_yield, "thread_yield");

	self = ulmk_thread_self();
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_thread_priority_get(self);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("thread_priority_get", mn, avg, mx);

	g_peer_ready = 0;
	peer = spawn("wpeer", park_peer, NULL, 0u, 1024u, 0u);
	wait_ready();
	g_target = peer;
	ulmk_board_hil_mark(6u);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_thread_priority_set(peer, 0u);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("thread_priority_set", mn, avg, mx);

	for (i = 0u; i < 2u; i++) {
		(void)ulmk_thread_suspend(peer);
		(void)ulmk_thread_resume(peer);
	}
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_thread_suspend(peer);
		samples[i] = slot_delta_after(seq);
		(void)ulmk_thread_resume(peer);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("thread_suspend", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		(void)ulmk_thread_suspend(peer);
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_thread_resume(peer);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("thread_resume", mn, avg, mx);

	/*
	 * Low-prio create/kill targets never need to run (same as silicon_e2e).
	 */
	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_tid_t t;

		seq = g_ulmk_syscall_wcet.seq;
		t = spawn("wtmp", low_idle, NULL, 200u, 512u, 0u);
		samples[i] = slot_delta_after(seq);
		if (t != ULMK_TID_INVALID) {
			ulmk_thread_kill(t);
			ulmk_thread_yield();
		}
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("thread_create", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_tid_t t = spawn("wkill", low_idle, NULL, 200u, 512u, 0u);

		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_thread_kill(t);
		samples[i] = slot_delta_after(seq);
		ulmk_thread_yield();
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("thread_kill", mn, avg, mx);
	record_custom("thread_exit", 0u, 0u, 0u, 1);
	ulmk_board_hil_mark(7u);

	board_console_puts("> ipc\n");
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		tmp = ulmk_ep_create();
		samples[i] = slot_delta_after(seq);
		ulmk_ep_destroy(tmp);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("ep_create", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		tmp = ulmk_ep_create();
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_ep_destroy(tmp);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("ep_destroy", mn, avg, mx);

	g_ipc_ep = ulmk_ep_create();
	g_peer_done = 0;
	g_srv_n = 0u;
	srv = spawn("wipc", ipc_server, NULL, 1u, 1024u, 0u);
	ulmk_ep_grant(g_ipc_ep, srv);
	/*
	 * Drop below the server so ep_reply returns to the server before
	 * we run again — otherwise reply "duration" includes our timeslice.
	 */
	(void)ulmk_thread_priority_set(self, 3u);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_ep_grant(g_ipc_ep, srv);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("ep_grant", mn, avg, mx);

	m.label = 0x100u;
	(void)ulmk_ep_call(g_ipc_ep, &m);
	{
		uint32_t blocked[WCET_SAMPLES];
		uint32_t bmn, bavg, bmx;

		for (i = 0u; i < WCET_SAMPLES; i++) {
			m.label = 0x100u;
			seq = g_ulmk_syscall_wcet.seq;
			(void)ulmk_ep_call(g_ipc_ep, &m);
			samples[i] = slot_delta_after(seq);
			blocked[i] = slot_blocked_after(seq);
		}
		stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
		stats_from_samples(blocked, WCET_SAMPLES, &bmn, &bavg, &bmx);
		(void)bmn;
		(void)bmx;
		record_blk("ep_call", mn, avg, mx, bavg);
	}

	m.label = 0xDEADu;
	(void)ulmk_ep_call(g_ipc_ep, &m);
	(void)ulmk_thread_priority_set(self, 0u);
	for (i = 0u; i < 4000u && !g_peer_done; i++)
		ulmk_thread_yield();
	if (g_srv_n > 0u) {
		stats_from_samples(g_srv_recv, g_srv_n, &mn, &avg, &mx);
		record("ep_recv", mn, avg, mx);
		stats_from_samples(g_srv_reply, g_srv_n, &mn, &avg, &mx);
		record("ep_reply", mn, avg, mx);
	} else {
		g_fail++;
		board_console_puts("ep_recv FAIL\n");
	}
	ulmk_thread_kill(srv);
	ulmk_ep_destroy(g_ipc_ep);

	/* ep_reply_recv — timed on the server side. */
	g_ipc_ep = ulmk_ep_create();
	g_peer_done = 0;
	g_peer_ready = 0;
	g_srv_rr_n = 0u;
	srv = spawn("wrr", ipc_rr_server, NULL, 1u, 1024u, 0u);
	ulmk_ep_grant(g_ipc_ep, srv);
	(void)ulmk_thread_priority_set(self, 3u);
	wait_ready();
	for (i = 0u; i < WCET_WARMUP + WCET_SAMPLES; i++) {
		m.label = 0x300u;
		(void)ulmk_ep_call(g_ipc_ep, &m);
	}
	m.label = 0xDEADu;
	(void)ulmk_ep_call(g_ipc_ep, &m);
	(void)ulmk_thread_priority_set(self, 0u);
	for (i = 0u; i < 4000u && !g_peer_done; i++)
		ulmk_thread_yield();
	if (g_srv_rr_n > WCET_WARMUP) {
		stats_from_samples(&g_srv_rr[WCET_WARMUP],
				   g_srv_rr_n - WCET_WARMUP, &mn, &avg, &mx);
		record("ep_reply_recv", mn, avg, mx);
	} else {
		g_fail++;
		board_console_puts("ep_reply_recv FAIL\n");
	}
	ulmk_thread_kill(srv);
	ulmk_ep_destroy(g_ipc_ep);

	ep = ulmk_ep_create();
	n = ulmk_notif_create();
	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_notif_signal(n, 0x1u);
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_ep_recv_or_notif(ep, n, 0x1u, &m, &sender, &bits);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("ep_recv_or_notif", mn, avg, mx);
	ulmk_ep_destroy(ep);

	board_console_puts("> notif\n");
	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_notif_t nc;

		seq = g_ulmk_syscall_wcet.seq;
		nc = ulmk_notif_create();
		samples[i] = slot_delta_after(seq);
		ulmk_notif_destroy(nc);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("notif_create", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_notif_signal(n, 0x1u);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("notif_signal", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_notif_signal(n, 0x1u);
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_notif_poll(n, 0x1u);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("notif_poll", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_notif_signal(n, 0x2u);
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_notif_wait(n, 0x2u, &bits);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("notif_wait", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		ulmk_notif_t x = ulmk_notif_create();

		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_notif_destroy(x);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("notif_destroy", mn, avg, mx);
	ulmk_notif_destroy(n);
	ulmk_board_hil_mark(8u);

	board_console_puts("> mem\n");
	for (s = 0u; s < 3u; s++) {
		for (i = 0u; i < WCET_SAMPLES; i++) {
			seq = g_ulmk_syscall_wcet.seq;
			p = ulmk_mem_map(NULL, sizes[s],
					 ULMK_PERM_READ | ULMK_PERM_WRITE,
					 ULMK_MMAP_ANON);
			samples[i] = slot_delta_after(seq);
			if (p)
				ulmk_mem_unmap(p, sizes[s]);
		}
		stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
		avgs[s] = avg;
		if (s == 0u)
			record("mem_map", mn, avg, mx);
	}
	slack = (avgs[0] * WCET_TOL_NUM) / WCET_TOL_DEN;
	if (slack < WCET_FLOOR_TICKS)
		slack = WCET_FLOOR_TICKS;
	ok = 1;
	if (avgs[1] > avgs[0] + slack || avgs[0] > avgs[1] + slack)
		ok = 0;
	if (avgs[2] > avgs[0] + slack || avgs[0] > avgs[2] + slack)
		ok = 0;
	record_custom("mem_map_size_o1", avgs[0], avgs[1], avgs[2], ok);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		p = ulmk_mem_map(NULL, 256u, ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_ANON);
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_mem_unmap(p, 256u);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("mem_unmap", mn, avg, mx);

	for (i = 0u; i < WCET_SAMPLES; i++) {
		p = ulmk_mem_map(NULL, 128u, ULMK_PERM_READ | ULMK_PERM_WRITE,
				 ULMK_MMAP_ANON);
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_mem_grant(p, 128u, g_target, ULMK_PERM_READ);
		samples[i] = slot_delta_after(seq);
		if (p)
			ulmk_mem_unmap(p, 128u);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("mem_grant", mn, avg, mx);

	board_console_puts("> irq\n");
	/*
	 * IRQ before heap_extend: each extend installs an MPU region; doing
	 * many extends first has been observed to Class-4 on the subsequent
	 * SRC write inside irq_bind on TC275.
	 */
	n = ulmk_notif_create();
	/*
	 * Soft irq_bind walks SRC_BASE+slot — one sample only on TC275.
	 * Measure that single kernel path; o1 uses a 0-width window (ok if
	 * delta captured).
	 */
	seq = g_ulmk_syscall_wcet.seq;
	(void)ulmk_irq_bind(5u, n, 0u);
	mn = slot_delta_after(seq);
	record_custom("irq_bind", mn, mn, mn, mn > 0u ? 1 : 0);
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_irq_enable(5u);
		samples[i] = slot_delta_after(seq);
		(void)ulmk_irq_disable(5u);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("irq_enable", mn, avg, mx);
	(void)ulmk_irq_enable(5u);
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_irq_ack(5u);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("irq_ack", mn, avg, mx);
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_irq_disable(5u);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("irq_disable", mn, avg, mx);
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_irq_bind_hw((uint8_t)(20u + i), n, 0u,
				       (uintptr_t)ULMK_BOARD_SRC_STM0_SR1);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("irq_bind_hw", mn, avg, mx);
	for (i = 0u; i < WCET_SAMPLES; i++) {
		seq = g_ulmk_syscall_wcet.seq;
		(void)ulmk_cap_grant(g_target, ULMK_CAP_SPAWN);
		samples[i] = slot_delta_after(seq);
	}
	stats_from_samples(samples, WCET_SAMPLES, &mn, &avg, &mx);
	record("cap_grant", mn, avg, mx);
	ulmk_notif_destroy(n);

	board_console_puts("> heap\n");
	g_done = ulmk_notif_create();
	(void)spawn("wheap", heap_worker, NULL, 1u, 2048u, 2048u);
	ulmk_notif_wait(g_done, 0x1u, &bits);
	ulmk_notif_destroy(g_done);

	/*
	 * One extend per fresh heap thread keeps region_count constant so the
	 * O(1) check is not confounded by MPU region growth.
	 */
	g_heap_ext_n = 0u;
	for (i = 0u; i < WCET_SAMPLES; i++)
		(void)spawn("whext", heap_extend_one, (void *)(uintptr_t)i,
			    1u, 1024u, 2048u);
	(void)ulmk_thread_priority_set(self, 3u);
	while (g_heap_ext_n < WCET_SAMPLES)
		ulmk_thread_yield();
	(void)ulmk_thread_priority_set(self, 0u);
	stats_from_samples(g_heap_ext, WCET_SAMPLES, &mn, &avg, &mx);
	record("heap_extend", mn, avg, mx);

	ulmk_thread_kill(g_target);
	ulmk_notif_destroy(g_park);

	ulmk_board_hil_mark(9u);

	if (g_fail == 0) {
		ulmk_board_hil_mark(0x5CE7u);
		board_console_puts("SILICON_WCET: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_WCET: FAIL fails=");
		put_u32((uint32_t)g_fail);
		board_console_putc('\n');
	}
	silicon_wcet_done();
	ulmk_thread_exit();
}
