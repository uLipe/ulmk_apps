/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*
 * Defaults for the ulmk FreeRTOS userspace shim.
 * Override by providing another FreeRTOSConfig.h earlier on the include path.
 */

#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ			1000
#endif

#ifndef configSTACK_DEPTH_TYPE
#define configSTACK_DEPTH_TYPE			uint16_t
#endif

#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES			32
#endif

#ifndef configMINIMAL_STACK_SIZE
#define configMINIMAL_STACK_SIZE		128
#endif

#ifndef configMAX_TASK_NAME_LEN
#define configMAX_TASK_NAME_LEN			16
#endif

#ifndef configULMK_MAX_TASKS
#define configULMK_MAX_TASKS			8
#endif

#ifndef configULMK_MAX_SEM
#define configULMK_MAX_SEM			8
#endif

#ifndef configULMK_MAX_QUEUES
#define configULMK_MAX_QUEUES			4
#endif

#ifndef configULMK_MAX_TIMERS
#define configULMK_MAX_TIMERS			4
#endif

#ifndef configULMK_QUEUE_POOL_BYTES
#define configULMK_QUEUE_POOL_BYTES		512
#endif

#ifndef configTIMER_TASK_PRIORITY
#define configTIMER_TASK_PRIORITY		(configMAX_PRIORITIES - 1)
#endif

#ifndef configTIMER_TASK_STACK_DEPTH
#define configTIMER_TASK_STACK_DEPTH		256
#endif

#ifndef configUSE_PREEMPTION
#define configUSE_PREEMPTION			1
#endif

#ifndef configUSE_MUTEXES
#define configUSE_MUTEXES			1
#endif

#ifndef configUSE_COUNTING_SEMAPHORES
#define configUSE_COUNTING_SEMAPHORES		1
#endif

#ifndef configUSE_TIMERS
#define configUSE_TIMERS			1
#endif

#ifndef configUSE_TASK_NOTIFICATIONS
#define configUSE_TASK_NOTIFICATIONS		1
#endif

#ifndef INCLUDE_vTaskDelay
#define INCLUDE_vTaskDelay			1
#endif

#ifndef INCLUDE_vTaskDelete
#define INCLUDE_vTaskDelete			1
#endif

#ifndef INCLUDE_vTaskSuspend
#define INCLUDE_vTaskSuspend			1
#endif

#ifndef INCLUDE_xTaskGetCurrentTaskHandle
#define INCLUDE_xTaskGetCurrentTaskHandle	1
#endif

#endif /* FREERTOS_CONFIG_H */
