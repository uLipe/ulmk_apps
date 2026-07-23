/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_PRIV_H
#define FREERTOS_PRIV_H

#include <stdint.h>
#include <ulmk/microkernel.h>
#include <ulmk/linker.h>

#include "FreeRTOS.h"

#define FR_WAKE_BIT		(1u << 0)
#define FR_NOTIFY_BIT		(1u << 1)

struct tskTaskControlBlock {
	ulmk_tid_t tid;
	ulmk_notif_t notif;
	volatile uint32_t notify_value;
	volatile uint8_t notify_pending;
	uint8_t used;
	uint8_t freertos_prio;
	char name[configMAX_TASK_NAME_LEN];
	TaskFunction_t entry;
	void *arg;
};

enum fr_obj_kind {
	FR_OBJ_SEM = 1,
	FR_OBJ_MUTEX,
	FR_OBJ_QUEUE,
};

/* Semaphores and mutexes share this header; queues extend it. */
struct QueueDefinition {
	uint8_t kind;
	uint8_t used;
	volatile uint32_t count;
	uint32_t max_count;
	ulmk_notif_t notif;
	ulmk_tid_t owner; /* mutex only */
	/* queue fields */
	uint8_t *storage;
	UBaseType_t item_size;
	UBaseType_t length;
	UBaseType_t head;
	UBaseType_t tail;
	UBaseType_t messages;
};

struct tmrTimerControl {
	uint8_t used;
	uint8_t active;
	uint8_t auto_reload;
	TickType_t period;
	TickType_t expiry;
	void *id;
	TimerCallbackFunction_t callback;
	char name[configMAX_TASK_NAME_LEN];
};

TickType_t fr_ticks_to_ms(TickType_t ticks);
struct tskTaskControlBlock *fr_task_from_tid(ulmk_tid_t tid);
struct tskTaskControlBlock *fr_task_current(void);
void fr_tick_service(void);
void fr_timers_on_tick(TickType_t now);

#endif /* FREERTOS_PRIV_H */
