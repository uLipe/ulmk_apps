# ulmk_apps

External components consumed by [ulmk](https://github.com/uLipe/ulmk) via the
sibling `../ulmk_apps` discovery path (`tools/dev.py` mounts it into the
container as `/ulmk_apps`).

## Cert set (TC275 Lite / board-provided BSP)

| Component | Role |
|-----------|------|
| [`silicon_baseline`](silicon_baseline/) | Bring-up: `root_thread` + console hello |
| [`silicon_e2e`](silicon_e2e/) | Public API smoke (`SILICON_E2E: PASS`) |
| [`silicon_unit`](silicon_unit/) | Per-syscall happy / edge / crash hardening |
| [`silicon_stress`](silicon_stress/) | Perf / isolation / footprint (`SILICON_STRESS: PASS`) |
| [`silicon_wcet`](silicon_wcet/) | Per-syscall WCET + O(1) ±10% (`SILICON_WCET: PASS`) |
| [`board_leds`](board_leds/) | Thin link to BSP LED API |

Board demos (`board_blinky`, `board_adc_pot`, …) live under
`ulmk_boards/<board>/components/` — not here.

Run order for silicon certs: baseline → e2e → unit → stress → wcet.

### Board contract

The BSP must provide:

- `board_services_init(info)` — console + timer bring-up
- `board_console_putc` / `board_console_puts`
- `board_timer_start` / `board_timer_sleep_us`
- `board_timer_now_ticks` / `board_timer_ticks_to_ns` (free-running counter)

Any board that meets this contract can run the same cert components.

### `silicon_unit`

Happy path, edge cases, and crash-hardening probes for public syscalls
(thread / ipc / notif / mem / heap / irq / cap).  Invalid handles must
return errors without `trap_panic`.  Report: `pass=N fail=M` then
`SILICON_UNIT: PASS|FAIL`.

### `silicon_wcet` report

Times every public userspace syscall in CPU cycles (CCNT / `slot=kern_pure`).
Each sample is wall-clock gateway time minus voluntary context-switch RTT
(`blocked`).  Lines are `name min/avg/max [blk=avg] o1=0|1`.  `o1=1` means
min/max stay within ±10% of avg (2-cycle floor).  `mem_map_size_o1` also checks
64/256/1024-byte maps.  `thread_exit` is `skip=noreturn`.  On TriCore silicon,
`irq_bind` is sampled once (dynamic SRC slot walk Class-4s on real TC275).

### Build / HIL (TC275 Lite)

```bash
# from ulmk/
python3 tools/dev.py build --board ../ulmk_boards/tc275_lite \
  --no-components --component silicon_unit

bash ../ulmk_boards/tc275_lite/scripts/hil-silicon-unit.sh \
  ../build/ulipe-tricore-tc275_lite/ulmk
```

ELF path note: `dev.py` writes under `../build/ulipe-<arch>-<board>/ulmk`, not `ulmk/build/`.
