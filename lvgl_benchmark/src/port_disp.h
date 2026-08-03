/* SPDX-License-Identifier: MIT */
#ifndef PORT_DISP_H
#define PORT_DISP_H

#include <lvgl.h>
#include <ulmk_device.h>

lv_display_t *port_disp_init(ulmk_dev_t *disp_dev);
void port_disp_blank(void);

#endif /* PORT_DISP_H */
