/* SPDX-License-Identifier: MIT */
/*
 * board_blinky — embedded hello: alternate LEDs @ 1 Hz + serial shell.
 *
 * Uses board_services_init_full(), board_leds_*, board_console_*, board_timer_*.
 * HIL: look for "BOARD_BLINKY: PASS" / "led1=" in the RAM console log.
 */

#include <stdint.h>
#include <stddef.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

void board_services_init_full(const ulmk_boot_info_t *info);
void board_console_putc(char c);
void board_console_puts(const char *s);
int board_console_getc(char *out);
void board_timer_sleep_us(uint32_t us);
int board_leds_set(uint32_t led, int on);
int board_leds_get(uint32_t led, int *on);
int board_leds_toggle(uint32_t led);
void ulmk_board_hil_mark(uint32_t n);

#define BOARD_LED_1	0u
#define BOARD_LED_2	1u

#define SHELL_LINE_MAX	32u
#define TICK_US		1000000u
#define POLL_SLICE_US	50000u

static ULMK_PRIVATE char g_line[SHELL_LINE_MAX];
static ULMK_PRIVATE uint32_t g_llen;
static ULMK_PRIVATE int g_phase;

static void put_u32(uint32_t v)
{
	char buf[10];
	int i = 0;

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

static int streq(const char *a, const char *b)
{
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void print_status(void)
{
	int a;
	int b;

	(void)board_leds_get(BOARD_LED_1, &a);
	(void)board_leds_get(BOARD_LED_2, &b);
	board_console_puts("led1=");
	board_console_putc(a ? '1' : '0');
	board_console_puts(" led2=");
	board_console_putc(b ? '1' : '0');
	board_console_putc('\n');
}

static void shell_help(void)
{
	board_console_puts("cmds: help status led1 on|off led2 on|off\n");
}

static void shell_exec(void)
{
	if (g_llen == 0u)
		return;
	g_line[g_llen] = '\0';
	if (streq(g_line, "help"))
		shell_help();
	else if (streq(g_line, "status"))
		print_status();
	else if (streq(g_line, "led1 on"))
		(void)board_leds_set(BOARD_LED_1, 1);
	else if (streq(g_line, "led1 off"))
		(void)board_leds_set(BOARD_LED_1, 0);
	else if (streq(g_line, "led2 on"))
		(void)board_leds_set(BOARD_LED_2, 1);
	else if (streq(g_line, "led2 off"))
		(void)board_leds_set(BOARD_LED_2, 0);
	else
		board_console_puts("?\n");
	g_llen = 0u;
}

static void shell_poll(void)
{
	char c;

	while (board_console_getc(&c) == ULMK_OK) {
		if (c == '\r' || c == '\n') {
			board_console_putc('\n');
			shell_exec();
		} else if (c == 0x08u || c == 0x7Fu) {
			if (g_llen > 0u) {
				g_llen--;
				board_console_puts("\b \b");
			}
		} else if (g_llen + 1u < SHELL_LINE_MAX) {
			g_line[g_llen++] = c;
			board_console_putc(c);
		}
	}
}

void __attribute__((noinline)) board_blinky_done(void)
{
}

void ulmk_root_thread(const ulmk_boot_info_t *info)
{
	uint32_t elapsed;
	uint32_t rounds;

	ulmk_board_hil_mark(1u);
	board_services_init_full(info);
	board_console_puts("BOARD_BLINKY: begin\n");
	board_console_puts("> leds+shell\n");
	shell_help();

	g_phase = 0;
	g_llen = 0u;
	rounds = 0u;

	/*
	 * Alternate LEDs every 1 s; poll shell in 50 ms slices so getc stays
	 * responsive.  After a few rounds print PASS for HIL smoke.
	 */
	for (;;) {
		if (g_phase == 0) {
			(void)board_leds_set(BOARD_LED_1, 1);
			(void)board_leds_set(BOARD_LED_2, 0);
		} else {
			(void)board_leds_set(BOARD_LED_1, 0);
			(void)board_leds_set(BOARD_LED_2, 1);
		}
		print_status();

		for (elapsed = 0u; elapsed < TICK_US; elapsed += POLL_SLICE_US) {
			shell_poll();
			board_timer_sleep_us(POLL_SLICE_US);
		}

		g_phase ^= 1;
		rounds++;
		if (rounds == 3u) {
			ulmk_board_hil_mark(0xB11Bu);
			board_console_puts("BOARD_BLINKY: PASS\n");
			board_blinky_done();
		}
	}
}
