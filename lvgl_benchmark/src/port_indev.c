/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <lvgl.h>
#include <ulmk_device.h>
#include <ulmk_device_input.h>
#include "port_indev.h"

static ulmk_dev_t *g_indev_dev;

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	struct ulmk_input_event ev;
	int n;

	(void)indev;
	if (!g_indev_dev) {
		data->state = LV_INDEV_STATE_RELEASED;
		return;
	}

	n = ulmk_input_read(g_indev_dev, &ev);
	if (n == (int)sizeof(ev) &&
	    ev.state == ULMK_INPUT_STATE_PRESSED) {
		data->state = LV_INDEV_STATE_PRESSED;
		data->point.x = (int32_t)ev.x;
		data->point.y = (int32_t)ev.y;
	} else {
		data->state = LV_INDEV_STATE_RELEASED;
	}
}

lv_indev_t *port_indev_init(ulmk_dev_t *indev_dev, lv_display_t *disp)
{
	lv_indev_t *indev;

	g_indev_dev = indev_dev;
	indev = lv_indev_create();
	if (!indev)
		return NULL;
	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, indev_read_cb);
	if (disp)
		lv_indev_set_display(indev, disp);
	return indev;
}
