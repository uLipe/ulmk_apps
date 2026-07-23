/* SPDX-License-Identifier: MIT */
#include "freertos_priv.h"

#include <string.h>

static ULMK_PRIVATE struct QueueDefinition g_queues[configULMK_MAX_QUEUES];
static ULMK_PRIVATE uint8_t g_qpool[configULMK_QUEUE_POOL_BYTES];
static ULMK_PRIVATE size_t g_qpool_used;

static void *qpool_alloc(size_t n)
{
	size_t off = g_qpool_used;

	if (off + n > sizeof(g_qpool))
		return NULL;
	g_qpool_used = off + n;
	return &g_qpool[off];
}

static struct QueueDefinition *queue_alloc(void)
{
	unsigned i;

	for (i = 0; i < configULMK_MAX_QUEUES; i++) {
		if (!g_queues[i].used) {
			memset(&g_queues[i], 0, sizeof(g_queues[i]));
			return &g_queues[i];
		}
	}
	return NULL;
}

static BaseType_t queue_send(struct QueueDefinition *q, const void *item,
			     TickType_t ticks, BaseType_t to_front)
{
	uint32_t bits;
	int err;
	uint8_t *slot;

	if (!q || !q->used || !item)
		return errQUEUE_FULL;

	for (;;) {
		if (q->messages < q->length) {
			if (to_front) {
				if (q->head == 0)
					q->head = q->length - 1u;
				else
					q->head--;
				slot = q->storage + q->head * q->item_size;
			} else {
				slot = q->storage + q->tail * q->item_size;
				q->tail++;
				if (q->tail >= q->length)
					q->tail = 0;
			}
			memcpy(slot, item, q->item_size);
			q->messages++;
			(void)ulmk_notif_signal(q->notif, FR_WAKE_BIT);
			return pdPASS;
		}

		if (ticks == 0)
			return errQUEUE_FULL;
		if (ticks == portMAX_DELAY)
			err = ulmk_notif_wait(q->notif, FR_WAKE_BIT, &bits);
		else
			err = ulmk_notif_wait_timeout(
				q->notif, FR_WAKE_BIT, &bits,
				(uint32_t)fr_ticks_to_ms(ticks));
		if (err != ULMK_OK)
			return errQUEUE_FULL;
		(void)ulmk_notif_poll(q->notif, FR_WAKE_BIT);
	}
}

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength,
			   const UBaseType_t uxItemSize)
{
	struct QueueDefinition *q;
	size_t bytes;

	if (uxQueueLength == 0 || uxItemSize == 0)
		return NULL;

	q = queue_alloc();
	if (!q)
		return NULL;

	bytes = (size_t)uxQueueLength * (size_t)uxItemSize;
	q->storage = qpool_alloc(bytes);
	if (!q->storage)
		return NULL;

	q->notif = ulmk_notif_create();
	if (q->notif == ULMK_NOTIF_INVALID)
		return NULL;

	q->kind = FR_OBJ_QUEUE;
	q->length = uxQueueLength;
	q->item_size = uxItemSize;
	q->head = 0;
	q->tail = 0;
	q->messages = 0;
	q->used = 1;
	return q;
}

BaseType_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue,
			    TickType_t xTicksToWait)
{
	return queue_send(xQueue, pvItemToQueue, xTicksToWait, pdFALSE);
}

BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
			     TickType_t xTicksToWait)
{
	return queue_send(xQueue, pvItemToQueue, xTicksToWait, pdTRUE);
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
			 TickType_t xTicksToWait)
{
	struct QueueDefinition *q = xQueue;
	uint32_t bits;
	int err;
	uint8_t *slot;

	if (!q || !q->used || !pvBuffer)
		return errQUEUE_EMPTY;

	for (;;) {
		if (q->messages > 0) {
			slot = q->storage + q->head * q->item_size;
			memcpy(pvBuffer, slot, q->item_size);
			q->head++;
			if (q->head >= q->length)
				q->head = 0;
			q->messages--;
			(void)ulmk_notif_signal(q->notif, FR_WAKE_BIT);
			return pdPASS;
		}

		if (xTicksToWait == 0)
			return errQUEUE_EMPTY;
		if (xTicksToWait == portMAX_DELAY)
			err = ulmk_notif_wait(q->notif, FR_WAKE_BIT, &bits);
		else
			err = ulmk_notif_wait_timeout(
				q->notif, FR_WAKE_BIT, &bits,
				(uint32_t)fr_ticks_to_ms(xTicksToWait));
		if (err != ULMK_OK)
			return errQUEUE_EMPTY;
		(void)ulmk_notif_poll(q->notif, FR_WAKE_BIT);
	}
}

BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void *pvItemToQueue)
{
	struct QueueDefinition *q = xQueue;

	if (!q || !q->used || q->length != 1 || !pvItemToQueue)
		return pdFAIL;

	if (q->messages == 0)
		return xQueueSendToBack(q, pvItemToQueue, 0);

	memcpy(q->storage, pvItemToQueue, q->item_size);
	(void)ulmk_notif_signal(q->notif, FR_WAKE_BIT);
	return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
	if (!xQueue || !xQueue->used)
		return 0;
	return xQueue->messages;
}

void vQueueDelete(QueueHandle_t xQueue)
{
	if (!xQueue || !xQueue->used)
		return;
	ulmk_notif_destroy(xQueue->notif);
	xQueue->used = 0;
}
