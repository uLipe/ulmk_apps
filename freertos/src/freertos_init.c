/* SPDX-License-Identifier: MIT */
#include "freertos_priv.h"

#include <string.h>

static ULMK_PRIVATE volatile TickType_t g_tick;
static ULMK_PRIVATE int g_inited;
static ULMK_PRIVATE int g_sched_started;

TickType_t fr_ticks_to_ms(TickType_t ticks)
{
	if (ticks == portMAX_DELAY)
		return portMAX_DELAY;
	if (ticks == 0)
		return 0;
	return (TickType_t)(((uint64_t)ticks * 1000ULL) /
			    (uint64_t)configTICK_RATE_HZ);
}

static void tick_entry(void *arg)
{
	(void)arg;

	for (;;) {
		ulmk_sleep_ms(1000u / (uint32_t)configTICK_RATE_HZ);
		g_tick++;
		fr_timers_on_tick(g_tick);
	}
}

void freertos_ulmk_init(void)
{
	ulmk_thread_attr_t attr;

	if (g_inited)
		return;
	g_inited = 1;
	g_tick = 0;

	memset(&attr, 0, sizeof(attr));
	attr.name = "fr_tick";
	attr.entry = tick_entry;
	attr.arg = NULL;
	attr.priority = 1; /* high (ulmk: 0 = highest) */
	attr.stack_size = 1024;
	attr.privilege = ULMK_PRIV_DRIVER;
	attr.cpu = 0;
	(void)ulmk_thread_create(&attr);
}

TickType_t xTaskGetTickCount(void)
{
	return g_tick;
}

void vTaskStartScheduler(void)
{
	g_sched_started = 1;
}

void vTaskEndScheduler(void)
{
	g_sched_started = 0;
}

BaseType_t xTaskGetSchedulerState(void)
{
	if (!g_inited)
		return taskSCHEDULER_NOT_STARTED;
	if (!g_sched_started)
		return taskSCHEDULER_NOT_STARTED;
	return taskSCHEDULER_RUNNING;
}

void fr_tick_service(void)
{
	/* reserved for future idle hooks */
}
