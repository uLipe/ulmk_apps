/* SPDX-License-Identifier: MIT */
#include "freertos_priv.h"

#include <string.h>

static ULMK_PRIVATE struct tmrTimerControl g_timers[configULMK_MAX_TIMERS];

void fr_timers_on_tick(TickType_t now)
{
	unsigned i;

	for (i = 0; i < configULMK_MAX_TIMERS; i++) {
		struct tmrTimerControl *t = &g_timers[i];

		if (!t->used || !t->active)
			continue;
		if ((int32_t)(now - t->expiry) < 0)
			continue;

		if (t->callback)
			t->callback(t);

		if (t->auto_reload) {
			t->expiry = now + t->period;
		} else {
			t->active = 0;
		}
	}
}

TimerHandle_t xTimerCreate(const char *const pcTimerName,
			   const TickType_t xTimerPeriodInTicks,
			   const BaseType_t xAutoReload,
			   void *const pvTimerID,
			   TimerCallbackFunction_t pxCallbackFunction)
{
	unsigned i;
	struct tmrTimerControl *t = NULL;

	if (xTimerPeriodInTicks == 0 || !pxCallbackFunction)
		return NULL;

	for (i = 0; i < configULMK_MAX_TIMERS; i++) {
		if (!g_timers[i].used) {
			t = &g_timers[i];
			break;
		}
	}
	if (!t)
		return NULL;

	memset(t, 0, sizeof(*t));
	if (pcTimerName) {
		strncpy(t->name, pcTimerName, configMAX_TASK_NAME_LEN - 1);
		t->name[configMAX_TASK_NAME_LEN - 1] = '\0';
	}
	t->period = xTimerPeriodInTicks;
	t->auto_reload = (xAutoReload != pdFALSE);
	t->id = pvTimerID;
	t->callback = pxCallbackFunction;
	t->active = 0;
	t->used = 1;
	return t;
}

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	(void)xTicksToWait;
	if (!xTimer || !xTimer->used)
		return pdFAIL;
	xTimer->expiry = xTaskGetTickCount() + xTimer->period;
	xTimer->active = 1;
	return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	(void)xTicksToWait;
	if (!xTimer || !xTimer->used)
		return pdFAIL;
	xTimer->active = 0;
	return pdPASS;
}

BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	return xTimerStart(xTimer, xTicksToWait);
}

BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
			      TickType_t xTicksToWait)
{
	(void)xTicksToWait;
	if (!xTimer || !xTimer->used || xNewPeriod == 0)
		return pdFAIL;
	xTimer->period = xNewPeriod;
	if (xTimer->active)
		xTimer->expiry = xTaskGetTickCount() + xNewPeriod;
	return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	(void)xTicksToWait;
	if (!xTimer || !xTimer->used)
		return pdFAIL;
	xTimer->active = 0;
	xTimer->used = 0;
	return pdPASS;
}

void *pvTimerGetTimerID(const TimerHandle_t xTimer)
{
	if (!xTimer || !xTimer->used)
		return NULL;
	return xTimer->id;
}
