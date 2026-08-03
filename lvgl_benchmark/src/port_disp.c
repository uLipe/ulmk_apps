/* SPDX-License-Identifier: MIT */
/*
 * LVGL display port — DIRECT dual-buffer via ulmk_device_manager display class.
 *
 * Paint into buffers from ulmk_disp_get_fb(); on last flush, pass dirty
 * rects via ulmk_disp_write_present() (ulmk_write) so the board adapter
 * owns cache maintenance + present.
 */
#include <stdint.h>
#include <lvgl.h>
#include <ulmk_device.h>
#include <ulmk_device_display.h>
#include "board_config.h"
#include "board_console.h"
#include "board_timer.h"
#include "port_disp.h"

#include "src/display/lv_display_private.h"

#define MAX_DIRTY	32

static ulmk_dev_t *g_disp_dev;
static uint16_t *g_fb0;
static uint16_t *g_fb1;
static uint32_t g_fb_bytes;
static uint32_t g_fps_frames;
static uint32_t g_fps_t0_ms;

static lv_area_t g_prev_dirty[MAX_DIRTY];
static uint16_t g_prev_n;
static lv_area_t g_cur_dirty[MAX_DIRTY];
static uint16_t g_cur_n;
static struct ulmk_disp_rect g_present_rects[MAX_DIRTY * 2u];

static void fps_kick(void)
{
	uint32_t now;

	g_fps_frames++;
	now = board_timer_now_ms();
	if (g_fps_t0_ms == 0u) {
		g_fps_t0_ms = now;
		return;
	}
	if ((now - g_fps_t0_ms) >= 5000u) {
		board_console_printf("lvgl fps=%u/5s (~%u)\r\n", g_fps_frames,
				     g_fps_frames / 5u);
		g_fps_frames = 0u;
		g_fps_t0_ms = now;
	}
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	uint16_t i;
	uint16_t n;
	struct ulmk_disp_rect *r;

	if (g_cur_n < MAX_DIRTY)
		g_cur_dirty[g_cur_n++] = *area;

	if (!lv_display_flush_is_last(disp)) {
		lv_display_flush_ready(disp);
		return;
	}

	n = 0u;
	for (i = 0; i < g_cur_n && n < (MAX_DIRTY * 2u); i++) {
		r = &g_present_rects[n++];
		r->x = (int16_t)g_cur_dirty[i].x1;
		r->y = (int16_t)g_cur_dirty[i].y1;
		r->w = (int16_t)(g_cur_dirty[i].x2 - g_cur_dirty[i].x1 + 1);
		r->h = (int16_t)(g_cur_dirty[i].y2 - g_cur_dirty[i].y1 + 1);
	}
	for (i = 0; i < g_prev_n && n < (MAX_DIRTY * 2u); i++) {
		r = &g_present_rects[n++];
		r->x = (int16_t)g_prev_dirty[i].x1;
		r->y = (int16_t)g_prev_dirty[i].y1;
		r->w = (int16_t)(g_prev_dirty[i].x2 - g_prev_dirty[i].x1 + 1);
		r->h = (int16_t)(g_prev_dirty[i].y2 - g_prev_dirty[i].y1 + 1);
	}

	(void)ulmk_disp_write_present(g_disp_dev, px_map,
				      n > 0u ? g_present_rects : NULL, n);
	fps_kick();

	g_prev_n = g_cur_n;
	for (i = 0; i < g_prev_n; i++)
		g_prev_dirty[i] = g_cur_dirty[i];
	g_cur_n = 0u;

	lv_display_flush_ready(disp);
}

lv_display_t *port_disp_init(ulmk_dev_t *disp_dev)
{
	lv_display_t *disp;
	void *fb0;
	void *fb1;
	uint32_t w;
	uint32_t h;
	uint32_t stride;
	uint32_t n_fb;
	int rc;

	if (!disp_dev)
		return NULL;
	g_disp_dev = disp_dev;

	rc = ulmk_disp_info(disp_dev, &w, &h, NULL, &stride, &n_fb);
	if (rc != ULMK_OK || w == 0u || h == 0u || n_fb < 2u)
		return NULL;

	g_fb_bytes = stride * h;
	rc = ulmk_disp_get_fb(disp_dev, 0u, &fb0);
	if (rc != ULMK_OK || !fb0)
		return NULL;
	rc = ulmk_disp_get_fb(disp_dev, 1u, &fb1);
	if (rc != ULMK_OK || !fb1)
		return NULL;

	g_fb0 = (uint16_t *)fb0;
	g_fb1 = (uint16_t *)fb1;
	lv_memzero(g_fb0, g_fb_bytes);
	lv_memzero(g_fb1, g_fb_bytes);
	(void)ulmk_disp_write_present(disp_dev, g_fb1, NULL, 0u);

	g_prev_n = 0u;
	g_cur_n = 0u;

	disp = lv_display_create((int32_t)w, (int32_t)h);
	if (!disp)
		return NULL;

	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(disp, flush_cb);
	lv_display_set_buffers(disp, g_fb0, g_fb1, g_fb_bytes,
			       LV_DISPLAY_RENDER_MODE_DIRECT);
	board_console_printf("lvgl DIRECT %ux%u via /dev/disp0\r\n",
			     (unsigned)w, (unsigned)h);
	return disp;
}

void port_disp_blank(void)
{
	lv_display_t *disp = lv_display_get_default();

	if (g_fb0)
		lv_memzero(g_fb0, g_fb_bytes);
	if (g_fb1)
		lv_memzero(g_fb1, g_fb_bytes);

	if (disp) {
		lv_ll_clear(&disp->sync_areas);
		disp->inv_p = 0;
		if (g_fb0 && g_fb1) {
			lv_display_set_buffers(disp, g_fb0, g_fb1, g_fb_bytes,
					       LV_DISPLAY_RENDER_MODE_DIRECT);
		}
	}

	g_prev_n = 0u;
	g_cur_n = 0u;
	if (g_disp_dev && g_fb1)
		(void)ulmk_disp_write_present(g_disp_dev, g_fb1, NULL, 0u);
}
