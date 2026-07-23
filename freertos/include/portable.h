/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_PORTABLE_H
#define FREERTOS_PORTABLE_H

#include <stdint.h>

typedef uint32_t StackType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY		((TickType_t)0xffffffffUL)
#define portTICK_PERIOD_MS	((TickType_t)(1000UL / (TickType_t)configTICK_RATE_HZ))

#define pdMS_TO_TICKS(xTimeInMs) \
	((TickType_t)(((TickType_t)(xTimeInMs) * (TickType_t)configTICK_RATE_HZ) / \
		      (TickType_t)1000U))

/* Critical sections intentionally unsupported in this shim. */
#define portENTER_CRITICAL()	do { } while (0)
#define portEXIT_CRITICAL()	do { } while (0)
#define taskENTER_CRITICAL()	portENTER_CRITICAL()
#define taskEXIT_CRITICAL()	portEXIT_CRITICAL()
#define taskDISABLE_INTERRUPTS()	do { } while (0)
#define taskENABLE_INTERRUPTS()		do { } while (0)

#endif /* FREERTOS_PORTABLE_H */
