/* SPDX-License-Identifier: MIT */
/*
 * FreeRTOS-compatible public header for the ulmk userspace shim.
 * Not affiliated with Amazon FreeRTOS; API names match FreeRTOS V10+.
 */
#ifndef FREERTOS_H
#define FREERTOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOSConfig.h"
#include "projdefs.h"
#include "portable.h"

#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

/**
 * Initialise the shim (tick + timer daemon). Call once from root before
 * creating FreeRTOS objects/tasks.
 */
void freertos_ulmk_init(void);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_H */
