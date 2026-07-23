/* SPDX-License-Identifier: MIT */
#include "freertos_priv.h"

#include <string.h>

static ULMK_PRIVATE struct tskTaskControlBlock g_tasks[configULMK_MAX_TASKS];

static uint8_t ulmk_prio_from_freertos(UBaseType_t prio)
{
	UBaseType_t max = (UBaseType_t)configMAX_PRIORITIES - 1u;

	if (prio > max)
		prio = max;
	/* FreeRTOS 0 = idle/low; ulmk 0 = highest */
	return (uint8_t)(max - prio);
}

struct tskTaskControlBlock *fr_task_from_tid(ulmk_tid_t tid)
{
	unsigned i;

	for (i = 0; i < configULMK_MAX_TASKS; i++) {
		if (g_tasks[i].used && g_tasks[i].tid == tid)
			return &g_tasks[i];
	}
	return NULL;
}

struct tskTaskControlBlock *fr_task_current(void)
{
	return fr_task_from_tid(ulmk_thread_self());
}

static void task_trampoline(void *arg)
{
	struct tskTaskControlBlock *tcb = arg;

	tcb->entry(tcb->arg);
	vTaskDelete(NULL);
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *const pcName,
		       const configSTACK_DEPTH_TYPE usStackDepth,
		       void *const pvParameters, UBaseType_t uxPriority,
		       TaskHandle_t *const pxCreatedTask)
{
	unsigned i;
	struct tskTaskControlBlock *tcb = NULL;
	ulmk_thread_attr_t attr;
	ulmk_tid_t tid;
	size_t stack_bytes;

	if (!pxTaskCode)
		return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;

	for (i = 0; i < configULMK_MAX_TASKS; i++) {
		if (!g_tasks[i].used) {
			tcb = &g_tasks[i];
			break;
		}
	}
	if (!tcb)
		return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;

	memset(tcb, 0, sizeof(*tcb));
	tcb->notif = ulmk_notif_create();
	if (tcb->notif == ULMK_NOTIF_INVALID)
		return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;

	tcb->entry = pxTaskCode;
	tcb->arg = pvParameters;
	tcb->freertos_prio = (uint8_t)uxPriority;
	if (pcName) {
		strncpy(tcb->name, pcName, configMAX_TASK_NAME_LEN - 1);
		tcb->name[configMAX_TASK_NAME_LEN - 1] = '\0';
	} else {
		tcb->name[0] = '\0';
	}

	stack_bytes = (size_t)usStackDepth * sizeof(StackType_t);
	if (stack_bytes < 512u)
		stack_bytes = 512u;

	memset(&attr, 0, sizeof(attr));
	attr.name = tcb->name[0] ? tcb->name : "fr_task";
	attr.entry = task_trampoline;
	attr.arg = tcb;
	attr.priority = ulmk_prio_from_freertos(uxPriority);
	attr.stack_size = stack_bytes;
	attr.privilege = ULMK_PRIV_DRIVER;
	attr.cpu = 0;

	tid = ulmk_thread_create(&attr);
	if ((int32_t)tid < 0 || tid == ULMK_TID_INVALID) {
		ulmk_notif_destroy(tcb->notif);
		return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
	}

	tcb->tid = tid;
	tcb->used = 1;
	if (pxCreatedTask)
		*pxCreatedTask = tcb;
	return pdPASS;
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
	struct tskTaskControlBlock *tcb;

	if (xTaskToDelete == NULL)
		tcb = fr_task_current();
	else
		tcb = xTaskToDelete;

	if (!tcb || !tcb->used) {
		if (xTaskToDelete == NULL)
			ulmk_thread_exit();
		return;
	}

	if (tcb->tid == ulmk_thread_self()) {
		tcb->used = 0;
		ulmk_notif_destroy(tcb->notif);
		ulmk_thread_exit();
	}

	(void)ulmk_thread_kill(tcb->tid);
	ulmk_notif_destroy(tcb->notif);
	tcb->used = 0;
}

void vTaskDelay(const TickType_t xTicksToDelay)
{
	TickType_t ms = fr_ticks_to_ms(xTicksToDelay);

	if (ms == 0)
		ms = 1;
	if (ms == portMAX_DELAY)
		ms = 0xffffffffu;
	(void)ulmk_sleep_ms((uint32_t)ms);
}

void vTaskDelayUntil(TickType_t *const pxPreviousWakeTime,
		     const TickType_t xTimeIncrement)
{
	TickType_t now;
	TickType_t target;
	TickType_t delay;

	if (!pxPreviousWakeTime || xTimeIncrement == 0)
		return;

	now = xTaskGetTickCount();
	target = *pxPreviousWakeTime + xTimeIncrement;
	if (target > now)
		delay = target - now;
	else
		delay = 0;
	*pxPreviousWakeTime = target;
	if (delay)
		vTaskDelay(delay);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
	return fr_task_current();
}

void vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
	struct tskTaskControlBlock *tcb =
		xTaskToSuspend ? xTaskToSuspend : fr_task_current();

	if (tcb && tcb->used)
		(void)ulmk_thread_suspend(tcb->tid);
}

void vTaskResume(TaskHandle_t xTaskToResume)
{
	if (xTaskToResume && xTaskToResume->used)
		(void)ulmk_thread_resume(xTaskToResume->tid);
}

UBaseType_t uxTaskPriorityGet(const TaskHandle_t xTask)
{
	struct tskTaskControlBlock *tcb =
		xTask ? xTask : fr_task_current();

	if (!tcb || !tcb->used)
		return 0;
	return tcb->freertos_prio;
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
	struct tskTaskControlBlock *tcb =
		xTask ? xTask : fr_task_current();

	if (!tcb || !tcb->used)
		return;
	tcb->freertos_prio = (uint8_t)uxNewPriority;
	(void)ulmk_thread_priority_set(tcb->tid,
				       ulmk_prio_from_freertos(uxNewPriority));
}

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue,
		       BaseType_t xAction)
{
	struct tskTaskControlBlock *tcb = xTaskToNotify;

	if (!tcb || !tcb->used)
		return pdFAIL;

	switch (xAction) {
	case eNoAction:
		break;
	case eSetBits:
		__atomic_or_fetch(&tcb->notify_value, ulValue, __ATOMIC_SEQ_CST);
		break;
	case eIncrement:
		__atomic_fetch_add(&tcb->notify_value, 1u, __ATOMIC_SEQ_CST);
		break;
	case eSetValueWithOverwrite:
		__atomic_store_n(&tcb->notify_value, ulValue, __ATOMIC_SEQ_CST);
		break;
	case eSetValueWithoutOverwrite:
		if (tcb->notify_pending)
			return pdFAIL;
		__atomic_store_n(&tcb->notify_value, ulValue, __ATOMIC_SEQ_CST);
		break;
	default:
		return pdFAIL;
	}

	tcb->notify_pending = 1;
	(void)ulmk_notif_signal(tcb->notif, FR_NOTIFY_BIT);
	return pdPASS;
}

BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify)
{
	return xTaskNotify(xTaskToNotify, 0, eIncrement);
}

BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
			   uint32_t ulBitsToClearOnExit,
			   uint32_t *pulNotificationValue,
			   TickType_t xTicksToWait)
{
	struct tskTaskControlBlock *tcb = fr_task_current();
	uint32_t bits;
	int err;

	if (!tcb)
		return pdFAIL;

	__atomic_and_fetch(&tcb->notify_value, ~ulBitsToClearOnEntry,
			   __ATOMIC_SEQ_CST);

	if (!tcb->notify_pending) {
		if (xTicksToWait == 0)
			return pdFALSE;
		if (xTicksToWait == portMAX_DELAY)
			err = ulmk_notif_wait(tcb->notif, FR_NOTIFY_BIT, &bits);
		else
			err = ulmk_notif_wait_timeout(
				tcb->notif, FR_NOTIFY_BIT, &bits,
				(uint32_t)fr_ticks_to_ms(xTicksToWait));
		if (err != ULMK_OK)
			return pdFALSE;
	}

	if (pulNotificationValue)
		*pulNotificationValue = tcb->notify_value;
	tcb->notify_pending = 0;
	__atomic_and_fetch(&tcb->notify_value, ~ulBitsToClearOnExit,
			   __ATOMIC_SEQ_CST);
	(void)ulmk_notif_poll(tcb->notif, FR_NOTIFY_BIT);
	return pdTRUE;
}

uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
	struct tskTaskControlBlock *tcb = fr_task_current();
	uint32_t val;
	uint32_t bits;
	int err;

	if (!tcb)
		return 0;

	while (tcb->notify_value == 0) {
		if (xTicksToWait == 0)
			return 0;
		if (xTicksToWait == portMAX_DELAY)
			err = ulmk_notif_wait(tcb->notif, FR_NOTIFY_BIT, &bits);
		else
			err = ulmk_notif_wait_timeout(
				tcb->notif, FR_NOTIFY_BIT, &bits,
				(uint32_t)fr_ticks_to_ms(xTicksToWait));
		if (err != ULMK_OK)
			return 0;
		(void)ulmk_notif_poll(tcb->notif, FR_NOTIFY_BIT);
	}

	val = tcb->notify_value;
	if (xClearCountOnExit)
		tcb->notify_value = 0;
	else if (val)
		tcb->notify_value = val - 1u;
	tcb->notify_pending = (tcb->notify_value != 0);
	return val;
}
