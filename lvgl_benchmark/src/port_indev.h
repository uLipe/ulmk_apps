/* SPDX-License-Identifier: MIT */
#ifndef PORT_INDEV_H
#define PORT_INDEV_H

#include <lvgl.h>
#include <ulmk_device.h>

lv_indev_t *port_indev_init(ulmk_dev_t *indev_dev, lv_display_t *disp);

#endif /* PORT_INDEV_H */
