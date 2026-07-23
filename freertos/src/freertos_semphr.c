/* SPDX-License-Identifier: MIT */
#include "freertos_priv.h"

#include <string.h>

static ULMK_PRIVATE struct QueueDefinition g_sems[configULMK_MAX_SEM];

static struct QueueDefinition *sem_alloc(void)
{
	unsigned i;

	for (i = 0; i < configULMK_MAX_SEM; i++) {
		if (!g_sems[i].used) {
			memset(&g_sems[i], 0, sizeof(g_sems[i]));
			return &g_sems[i];
		}
	}
	return NULL;
}

static BaseType_t sem_take(struct QueueDefinition *s, TickType_t ticks)
{
	uint32_t bits;
	int err;
	uint32_t cur;

	if (!s || !s->used)
		return pdFALSE;

	for (;;) {
		cur = __atomic_load_n(&s->count, __ATOMIC_SEQ_CST);
		if (s->kind == FR_OBJ_MUTEX) {
			if (s->owner == ulmk_thread_self())
				return pdTRUE;
			if (cur == 0) {
				if (__atomic_compare_exchange_n(
					    &s->count, &cur, 1u, 0,
					    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
					s->owner = ulmk_thread_self();
					return pdTRUE;
				}
				continue;
			}
		} else if (cur > 0) {
			if (__atomic_compare_exchange_n(&s->count, &cur, cur - 1u,
							0, __ATOMIC_SEQ_CST,
							__ATOMIC_SEQ_CST))
				return pdTRUE;
			continue;
		}

		if (ticks == 0)
			return pdFALSE;
		if (ticks == portMAX_DELAY)
			err = ulmk_notif_wait(s->notif, FR_WAKE_BIT, &bits);
		else
			err = ulmk_notif_wait_timeout(
				s->notif, FR_WAKE_BIT, &bits,
				(uint32_t)fr_ticks_to_ms(ticks));
		if (err != ULMK_OK)
			return pdFALSE;
		(void)ulmk_notif_poll(s->notif, FR_WAKE_BIT);
	}
}

static BaseType_t sem_give(struct QueueDefinition *s)
{
	uint32_t cur;

	if (!s || !s->used)
		return pdFALSE;

	if (s->kind == FR_OBJ_MUTEX) {
		if (s->owner != ulmk_thread_self())
			return pdFALSE;
		s->owner = ULMK_TID_INVALID;
		__atomic_store_n(&s->count, 0u, __ATOMIC_SEQ_CST);
		(void)ulmk_notif_signal(s->notif, FR_WAKE_BIT);
		return pdTRUE;
	}

	cur = __atomic_load_n(&s->count, __ATOMIC_SEQ_CST);
	if (cur >= s->max_count)
		return pdFALSE;
	if (!__atomic_compare_exchange_n(&s->count, &cur, cur + 1u, 0,
					 __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return sem_give(s);

	(void)ulmk_notif_signal(s->notif, FR_WAKE_BIT);
	return pdTRUE;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	struct QueueDefinition *s = sem_alloc();

	if (!s)
		return NULL;
	s->notif = ulmk_notif_create();
	if (s->notif == ULMK_NOTIF_INVALID)
		return NULL;
	s->kind = FR_OBJ_SEM;
	s->max_count = 1;
	s->count = 0; /* empty — must Give before Take */
	s->used = 1;
	return s;
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
					   UBaseType_t uxInitialCount)
{
	struct QueueDefinition *s = sem_alloc();

	if (!s || uxMaxCount == 0)
		return NULL;
	if (uxInitialCount > uxMaxCount)
		uxInitialCount = uxMaxCount;
	s->notif = ulmk_notif_create();
	if (s->notif == ULMK_NOTIF_INVALID)
		return NULL;
	s->kind = FR_OBJ_SEM;
	s->max_count = uxMaxCount;
	s->count = uxInitialCount;
	s->used = 1;
	return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	struct QueueDefinition *s = sem_alloc();

	if (!s)
		return NULL;
	s->notif = ulmk_notif_create();
	if (s->notif == ULMK_NOTIF_INVALID)
		return NULL;
	s->kind = FR_OBJ_MUTEX;
	s->max_count = 1;
	s->count = 0; /* 0 = free, 1 = taken */
	s->owner = ULMK_TID_INVALID;
	s->used = 1;
	return s;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
	return sem_take(xSemaphore, xTicksToWait);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
	return sem_give(xSemaphore);
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	struct QueueDefinition *s = xSemaphore;

	if (!s || !s->used)
		return;
	ulmk_notif_destroy(s->notif);
	s->used = 0;
}
