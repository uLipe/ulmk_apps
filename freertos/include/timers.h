/* SPDX-License-Identifier: MIT */
#ifndef FREERTOS_TIMERS_H
#define FREERTOS_TIMERS_H

#include "FreeRTOSConfig.h"
#include "projdefs.h"
#include "portable.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tmrTimerControl;
typedef struct tmrTimerControl *TimerHandle_t;

typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

#define tmrCOMMAND_START		0
#define tmrCOMMAND_STOP			1
#define tmrCOMMAND_CHANGE_PERIOD	2
#define tmrCOMMAND_DELETE		3

TimerHandle_t xTimerCreate(const char *const pcTimerName,
			   const TickType_t xTimerPeriodInTicks,
			   const BaseType_t xAutoReload,
			   void *const pvTimerID,
			   TimerCallbackFunction_t pxCallbackFunction);

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
			      TickType_t xTicksToWait);
BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait);
void *pvTimerGetTimerID(const TimerHandle_t xTimer);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_TIMERS_H */
