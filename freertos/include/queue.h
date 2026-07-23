/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_QUEUE_H
#define FREERTOS_QUEUE_H

#include "FreeRTOSConfig.h"
#include "projdefs.h"
#include "portable.h"

#ifdef __cplusplus
extern "C" {
#endif

struct QueueDefinition;
typedef struct QueueDefinition *QueueHandle_t;

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength,
			   const UBaseType_t uxItemSize);
BaseType_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue,
			    TickType_t xTicksToWait);
BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
			     TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
			 TickType_t xTicksToWait);
BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void *pvItemToQueue);
UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue);
void vQueueDelete(QueueHandle_t xQueue);

#define xQueueSend(xQueue, pvItemToQueue, xTicksToWait) \
	xQueueSendToBack((xQueue), (pvItemToQueue), (xTicksToWait))

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_QUEUE_H */
