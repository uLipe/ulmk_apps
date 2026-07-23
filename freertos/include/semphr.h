/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_SEMPHR_H
#define FREERTOS_SEMPHR_H

#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef QueueHandle_t SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
					   UBaseType_t uxInitialCount);
SemaphoreHandle_t xSemaphoreCreateMutex(void);

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_SEMPHR_H */
