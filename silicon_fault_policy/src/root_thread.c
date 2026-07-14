/* SPDX-License-Identifier: MIT */
/*
 * silicon_fault_policy — HIL mirror of sdk_suite/fault_policy.
 * HIL: SILICON_FAULT_POLICY: PASS  scratch 0xFA17  silicon_fault_policy_done().
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

void __attribute__((noinline)) silicon_fault_policy_done(void);

static void sdk_puts(const char *s)
{
	board_console_puts(s);
}

static void sdk_put_u32(uint32_t v)
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

static int sdk_map_ok(const void *p)
{
	uintptr_t u = (uintptr_t)p;

	if (u == 0u)
		return 0;
	if (u >= 0x80000000u)
		return 1;
	return (intptr_t)u > 0;
}

static ulmk_tid_t sdk_spawn_priv(const char *name, void (*entry)(void *),
				 void *arg, uint8_t prio, size_t stack,
				 size_t heap, ulmk_privilege_t priv)
{
	ulmk_thread_attr_t a;

	a.name       = name;
	a.entry      = entry;
	a.arg        = arg;
	a.priority   = prio;
	a.stack_size = stack;
	a.privilege  = priv;
	a.heap_size  = heap;
	return ulmk_thread_create(&a);
}

#define BIT_GO		(1u << 0)
#define BIT_ACK		(1u << 1)
#define STACK_SZ	1024u

static ULMK_PRIVATE ulmk_notif_t g_sync;
static ULMK_PRIVATE volatile uint32_t *g_forbidden;
static ULMK_PRIVATE volatile int g_user_armed;
static ULMK_PRIVATE volatile int g_user_survived;
static ULMK_PRIVATE volatile int g_drv_armed;
static ULMK_PRIVATE volatile int g_drv_survived;
static ULMK_PRIVATE volatile int g_root_armed;
static ULMK_PRIVATE volatile int g_root_survived;

static void bad_write(void)
{
	g_forbidden[0] = 0xDEADBEEFu;
}

static void user_faulter(void *arg)
{
	uint32_t bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	g_user_armed = 1;
	ulmk_notif_signal(g_sync, BIT_ACK);
	bad_write();
	g_user_survived = 1;
	ulmk_thread_exit();
}

static void drv_faulter(void *arg)
{
	uint32_t bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	g_drv_armed = 1;
	ulmk_notif_signal(g_sync, BIT_ACK);
	bad_write();
	g_drv_survived = 1;
	ulmk_thread_exit();
}

static void root_faulter(void *arg)
{
	uint32_t bits = 0u;

	(void)arg;
	ulmk_notif_wait(g_sync, BIT_GO, &bits);
	g_root_armed = 1;
	ulmk_notif_signal(g_sync, BIT_ACK);
	bad_write();
	g_root_survived = 1;
	ulmk_thread_exit();
}

static int run_fault_case(void (*entry)(void *), ulmk_privilege_t priv,
			  volatile int *armed, volatile int *survived,
			  const char *tag, uint32_t caps)
{
	ulmk_tid_t tid;
	uint32_t   bits = 0u;
	int        i;
	int        dead;

	*armed = 0;
	*survived = 0;
	tid = sdk_spawn_priv(tag, entry, NULL, 20u, STACK_SZ, 0u, priv);
	if (tid == ULMK_TID_INVALID)
		return 0;
	if (caps != 0u && ulmk_cap_grant(tid, caps) != ULMK_OK)
		return 0;

	ulmk_notif_signal(g_sync, BIT_GO);
	bits = 0u;
	ulmk_notif_wait(g_sync, BIT_ACK, &bits);
	for (i = 0; i < 400; i++)
		ulmk_thread_yield();

	dead = (ulmk_thread_priority_get(tid) < 0) || (*armed && !*survived);
	sdk_puts("SILICON_FAULT_POLICY: ");
	sdk_puts(tag);
	sdk_puts(dead && *armed && !*survived ? " PASS\n" : " FAIL\n");
	return dead && *armed && !*survived;
}

void __attribute__((noinline)) silicon_fault_policy_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	void *page;
	int   u_ok;
	int   d_ok;
	int   r_ok;

	ulmk_board_hil_mark(1u);
	board_services_init(info);
	ulmk_board_hil_mark(3u);
	sdk_puts("SILICON_FAULT_POLICY: begin\n");

	g_sync = ulmk_notif_create();
	page = ulmk_mem_map(NULL, 128u, ULMK_PERM_READ | ULMK_PERM_WRITE,
			    ULMK_MMAP_ANON);
	if (!sdk_map_ok(page) || g_sync == ULMK_NOTIF_INVALID) {
		ulmk_board_hil_mark(0xDEADu);
		sdk_puts("SILICON_FAULT_POLICY: FAIL\n");
		silicon_fault_policy_done();
		ulmk_thread_exit();
	}
	g_forbidden = (volatile uint32_t *)page;

	u_ok = run_fault_case(user_faulter, ULMK_PRIV_USER,
			      &g_user_armed, &g_user_survived, "user", 0u);
	d_ok = run_fault_case(drv_faulter, ULMK_PRIV_DRIVER,
			      &g_drv_armed, &g_drv_survived, "driver", 0u);
	r_ok = run_fault_case(root_faulter, ULMK_PRIV_DRIVER,
			      &g_root_armed, &g_root_survived, "root_caps",
			      ULMK_CAP_ALL);

	sdk_puts("SILICON_FAULT_POLICY: u=");
	sdk_put_u32((uint32_t)u_ok);
	sdk_puts(" d=");
	sdk_put_u32((uint32_t)d_ok);
	sdk_puts(" r=");
	sdk_put_u32((uint32_t)r_ok);
	sdk_puts("\n");
	if (u_ok && d_ok && r_ok) {
		ulmk_board_hil_mark(0xFA17u);
		sdk_puts("SILICON_FAULT_POLICY: PASS\n");
	} else {
		ulmk_board_hil_mark(0xDEADu);
		sdk_puts("SILICON_FAULT_POLICY: FAIL\n");
	}
	silicon_fault_policy_done();
	(void)ulmk_mem_unmap(page, 128u);
	ulmk_thread_exit();
}
