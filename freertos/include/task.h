/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include "FreeRTOSConfig.h"
#include "projdefs.h"
#include "portable.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tskTaskControlBlock;
typedef struct tskTaskControlBlock *TaskHandle_t;

typedef enum {
	eRunning = 0,
	eReady,
	eBlocked,
	eSuspended,
	eDeleted,
	eInvalid
} eTaskState;

#define tskIDLE_PRIORITY		((UBaseType_t)0U)

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
		       const char *const pcName,
		       const configSTACK_DEPTH_TYPE usStackDepth,
		       void *const pvParameters,
		       UBaseType_t uxPriority,
		       TaskHandle_t *const pxCreatedTask);

void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskDelay(const TickType_t xTicksToDelay);
void vTaskDelayUntil(TickType_t *const pxPreviousWakeTime,
		     const TickType_t xTimeIncrement);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskSuspend(TaskHandle_t xTaskToSuspend);
void vTaskResume(TaskHandle_t xTaskToResume);
UBaseType_t uxTaskPriorityGet(const TaskHandle_t xTask);
void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);
void vTaskStartScheduler(void);
void vTaskEndScheduler(void);
BaseType_t xTaskGetSchedulerState(void);

#define taskSCHEDULER_NOT_STARTED	((BaseType_t)1)
#define taskSCHEDULER_RUNNING		((BaseType_t)2)
#define taskSCHEDULER_SUSPENDED		((BaseType_t)0)

/* Task notifications (FreeRTOS V10 style, index 0 only). */
BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue,
		       BaseType_t xAction);
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
			   uint32_t ulBitsToClearOnExit,
			   uint32_t *pulNotificationValue,
			   TickType_t xTicksToWait);
BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify);
uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);

/* xAction values for xTaskNotify */
#define eNoAction			0
#define eSetBits			1
#define eIncrement			2
#define eSetValueWithOverwrite		3
#define eSetValueWithoutOverwrite	4

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_TASK_H */
