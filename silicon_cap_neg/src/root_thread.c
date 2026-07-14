/* SPDX-License-Identifier: MIT */
/*
 * silicon_cap_neg — capability / privilege negative matrix (HIL).
 *
 * USER (IO=0, caps=0): DRIVER-gated and CAP-gated syscalls → ULMK_EPERM.
 * DRIVER without SPAWN/KILL/GRANT_CAP: those CAP gates → ULMK_EPERM;
 * seeded MAP_PERIPH|IRQ still work (positive control).
 *
 * HIL: SILICON_CAP_NEG: PASS  scratch 0xCA91  silicon_cap_neg_done().
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

/*
 * MMIO window used only to probe ULMK_MMAP_PERIPH + CAP.  Prefer the board
 * constant when the BSP puts board_config.h on the include path.
 */
#ifndef ULMK_BOARD_PERIPH_BASE
#define ULMK_BOARD_PERIPH_BASE	0xF0000000u
#endif

void board_services_init(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
void ulmk_board_hil_mark(uint32_t n);

/* QEMU boards may omit HIL scratch; silicon BSP provides a strong symbol. */
__attribute__((weak)) void ulmk_board_hil_mark(uint32_t n)
{
	(void)n;
}

static ULMK_PRIVATE int g_pass;
static ULMK_PRIVATE int g_fail;
static ULMK_PRIVATE ulmk_notif_t g_done;
static ULMK_PRIVATE volatile int g_user_spawn_eperm;
static ULMK_PRIVATE volatile int g_user_kill_eperm;
static ULMK_PRIVATE volatile int g_user_sus_eperm;
static ULMK_PRIVATE volatile int g_user_irq_eperm;
static ULMK_PRIVATE volatile int g_user_heap_eperm;
static ULMK_PRIVATE volatile int g_user_mmap_eperm;
static ULMK_PRIVATE volatile int g_user_cap_eperm;
static ULMK_PRIVATE volatile int g_drv_spawn_eperm;
static ULMK_PRIVATE volatile int g_drv_kill_eperm;
static ULMK_PRIVATE volatile int g_drv_cap_eperm;
static ULMK_PRIVATE volatile int g_drv_irq_ok;
static ULMK_PRIVATE volatile int g_drv_mmap_ok;
static ULMK_PRIVATE ulmk_tid_t g_victim;

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

static int is_eperm(int rc)
{
	return rc == ULMK_EPERM;
}

/*
 * SPAWN failures surface as the raw errno in the TID word (router returns
 * before ulmk_kern_thread_spawn).  ULMK_TID_INVALID (0) is a pool/attr fail.
 */
static int tid_is_eperm(ulmk_tid_t tid)
{
	return (int32_t)(uintptr_t)tid == ULMK_EPERM;
}

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
			uint8_t prio, size_t heap, ulmk_privilege_t priv)
{
	ulmk_thread_attr_t a;

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = 1024u;
	a.privilege  = priv;
	a.heap_size  = heap;
	return ulmk_thread_create(&a);
}

static void idle_victim(void *arg)
{
	(void)arg;
	for (;;)
		ulmk_thread_yield();
}

static void user_probe(void *arg)
{
	ulmk_thread_attr_t a;
	ulmk_notif_t       n;
	void              *p;
	ulmk_tid_t         tid;

	(void)arg;

	a.name       = "x";
	a.entry      = idle_victim;
	a.arg        = NULL;
	a.priority   = 200u;
	a.stack_size = 512u;
	a.privilege  = ULMK_PRIV_USER;
	a.heap_size  = 0u;
	tid = ulmk_thread_create(&a);
	g_user_spawn_eperm = tid_is_eperm(tid);

	g_user_kill_eperm = is_eperm(ulmk_thread_kill(g_victim));
	g_user_sus_eperm  = is_eperm(ulmk_thread_suspend(g_victim));

	n = ulmk_notif_create();
	g_user_irq_eperm = is_eperm(ulmk_irq_bind(5u, n, 0u));
	if (n != ULMK_NOTIF_INVALID)
		ulmk_notif_destroy(n);

	g_user_heap_eperm = is_eperm(ulmk_heap_extend(64u));

	p = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_PERIPH_BASE, 64u,
			 ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	g_user_mmap_eperm = (p == NULL);

	g_user_cap_eperm = is_eperm(ulmk_cap_grant(g_victim, ULMK_CAP_SPAWN));

	ulmk_notif_signal(g_done, 0x1u);
	ulmk_thread_exit();
}

static void driver_probe(void *arg)
{
	ulmk_thread_attr_t a;
	ulmk_notif_t       n;
	void              *p;
	ulmk_tid_t         tid;

	(void)arg;

	a.name       = "y";
	a.entry      = idle_victim;
	a.arg        = NULL;
	a.priority   = 200u;
	a.stack_size = 512u;
	a.privilege  = ULMK_PRIV_USER;
	a.heap_size  = 0u;
	tid = ulmk_thread_create(&a);
	g_drv_spawn_eperm = tid_is_eperm(tid);

	g_drv_kill_eperm = is_eperm(ulmk_thread_kill(g_victim));
	g_drv_cap_eperm  = is_eperm(ulmk_cap_grant(g_victim, ULMK_CAP_SPAWN));

	n = ulmk_notif_create();
	g_drv_irq_ok = (n != ULMK_NOTIF_INVALID) &&
		       (ulmk_irq_bind(6u, n, 0u) == ULMK_OK);
	if (n != ULMK_NOTIF_INVALID)
		ulmk_notif_destroy(n);

	p = ulmk_mem_map((void *)(uintptr_t)ULMK_BOARD_PERIPH_BASE, 64u,
			 ULMK_PERM_READ | ULMK_PERM_WRITE, ULMK_MMAP_PERIPH);
	g_drv_mmap_ok = map_ok(p);
	if (g_drv_mmap_ok)
		(void)ulmk_mem_unmap(p, 64u);

	ulmk_notif_signal(g_done, 0x2u);
	ulmk_thread_exit();
}

void __attribute__((noinline)) silicon_cap_neg_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t bits = 0u;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);

	board_console_puts("SILICON_CAP_NEG: begin\n");
	g_pass = 0;
	g_fail = 0;

	g_done = ulmk_notif_create();
	g_victim = spawn("victim", idle_victim, NULL, 200u, 0u,
			 ULMK_PRIV_USER);
	CHECK("victim", g_victim != ULMK_TID_INVALID);

	progress("user/eperm");
	spawn("uprobe", user_probe, NULL, 10u, 512u, ULMK_PRIV_USER);
	bits = 0u;
	ulmk_notif_wait(g_done, 0x1u, &bits);
	CHECK("u_spawn", g_user_spawn_eperm);
	CHECK("u_kill", g_user_kill_eperm);
	CHECK("u_sus", g_user_sus_eperm);
	CHECK("u_irq", g_user_irq_eperm);
	CHECK("u_heap", g_user_heap_eperm);
	CHECK("u_mmap", g_user_mmap_eperm);
	CHECK("u_cap", g_user_cap_eperm);

	progress("drv/cap");
	spawn("dprobe", driver_probe, NULL, 10u, 0u, ULMK_PRIV_DRIVER);
	bits = 0u;
	ulmk_notif_wait(g_done, 0x2u, &bits);
	CHECK("d_spawn", g_drv_spawn_eperm);
	CHECK("d_kill", g_drv_kill_eperm);
	CHECK("d_cap", g_drv_cap_eperm);
	CHECK("d_irq_ok", g_drv_irq_ok);
	CHECK("d_mmap_ok", g_drv_mmap_ok);

	progress("root/pos");
	CHECK("r_spawn",
	      spawn("ok", idle_victim, NULL, 200u, 0u, ULMK_PRIV_USER) !=
	      ULMK_TID_INVALID);
	CHECK("r_cap", ulmk_cap_grant(g_victim, ULMK_CAP_SPAWN) == ULMK_OK);

	board_console_puts("SILICON_CAP_NEG: REPORT\n");
	board_console_puts("pass=");
	put_u32((uint32_t)g_pass);
	board_console_puts(" fail=");
	put_u32((uint32_t)g_fail);
	board_console_putc('\n');

	if (g_fail == 0) {
		ulmk_board_hil_mark(0xCA91u);
		board_console_puts("SILICON_CAP_NEG: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		board_console_puts("SILICON_CAP_NEG: FAIL\n");
	}

	silicon_cap_neg_done();
	ulmk_thread_exit();
}
