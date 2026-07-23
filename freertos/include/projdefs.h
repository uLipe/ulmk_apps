/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_PROJDEFS_H
#define FREERTOS_PROJDEFS_H

#include <stdint.h>

typedef void (*TaskFunction_t)(void *pvParameters);

#define pdFALSE			((BaseType_t)0)
#define pdTRUE			((BaseType_t)1)
#define pdPASS			(pdTRUE)
#define pdFAIL			(pdFALSE)

#define errQUEUE_EMPTY		((-1))
#define errQUEUE_FULL		((-2))
#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY	((-3))

#endif /* FREERTOS_PROJDEFS_H */
