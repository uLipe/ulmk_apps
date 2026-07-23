# FreeRTOS userspace shim (ulmk_apps)

MIT-licensed **API-compatible** FreeRTOS V10+ façade that runs in userspace on
top of the ulmk microkernel. Lives in **ulmk_apps** (not the kernel tree).
This is **not** a second scheduler and does **not** vendor the FreeRTOS kernel.

Enable with any app/board that `REQUIRES freertos`, e.g. the TC275 demo
`freertos_sem_demo`.

## Mapping

| FreeRTOS | ulmk |
|----------|------|
| Task | `ulmk_thread_create` / `exit` / `kill` / `yield` / `suspend` / `resume` |
| `vTaskDelay` | `ulmk_sleep_ms` |
| Semaphore / mutex | userspace counter + `ulmk_notif` |
| Queue | ring buffer + `ulmk_notif` |
| Task notify | per-task `ulmk_notif` + value |
| Software timer | tick thread callbacks |
| `vTaskStartScheduler` | noop (ulmk already schedules) |

## Limitations (v1)

- No `taskENTER_CRITICAL` / arch port / `FromISR` APIs.
- Mutex has **no** priority inheritance.
- FreeRTOS tasks are created as `ULMK_PRIV_DRIVER` (console/board IPC).
- `xTaskCreate` needs `ULMK_CAP_SPAWN` (typically root).
- Queue/sem waiters are not fully SMP-safe under concurrent multi-producer races.

## Init

Call `freertos_ulmk_init()` once from `ulmk_root_thread` before creating
objects/tasks (starts the 1 ms tick thread used by timers and
`xTaskGetTickCount`).

App/demo statics that FreeRTOS tasks touch must use `ULMK_PRIVATE` (component
domain BSS). Plain `.bss` from a component library can land in kernel RAM and
fault under the user MPU.

## Demo (TC275 Lite)

```bash
python3 tools/dev.py build --board ../ulmk_boards/tc275_lite --clean \
  --no-components --component freertos --component freertos_sem_demo
# flash + UART: expect "freertos: Task A" / "Task B got sem"
```
