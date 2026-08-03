/* SPDX-License-Identifier: MIT */
#ifndef APP_BENCHMARK_H
#define APP_BENCHMARK_H

/*
 * Pulled into LVGL via LV_ASSERT_HANDLER — keep free of ulmk_device.h so
 * the LVGL static libs do not need the device-manager include graph.
 */
void app_lvgl_assert(void);

#endif /* APP_BENCHMARK_H */
