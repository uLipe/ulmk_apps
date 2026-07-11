/* SPDX-License-Identifier: MIT */
/*
 * board_leds_api.c — thin re-export so apps can DEPEND on board_leds.
 * Implementation lives in the tc275_lite BSP (board_leds.c).
 */

#include <board_leds.h>

/* Ensure the component archive is non-empty for the linker. */
int board_leds_component_marker(void)
{
	return BOARD_LED_COUNT;
}
