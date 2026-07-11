# ulmk_apps

External components consumed by [ulmk](https://github.com/uLipe/ulmk) via the
sibling `../ulmk_apps` discovery path (`tools/dev.py` mounts it into the
container as `/ulmk_apps`).

## Cert set (TC275 Lite / board-provided BSP)

| Component | Role |
|-----------|------|
| [`silicon_baseline`](silicon_baseline/) | Bring-up: `root_thread` + console hello |
| [`silicon_e2e`](silicon_e2e/) | Public API correctness (`SILICON_E2E: PASS`) |
| [`silicon_stress`](silicon_stress/) | Perf / isolation / footprint (`SILICON_STRESS: PASS` + REPORT) |

### Board contract

The BSP must provide:

- `board_services_init(info)` — console + timer bring-up
- `board_console_putc` / `board_console_puts`
- `board_timer_start` / `board_timer_sleep_us`
- `board_timer_now_ticks` / `board_timer_ticks_to_ns` (free-running counter)

Any board that meets this contract can run the same cert components.

### Build / HIL (TC275 Lite)

```bash
# from ulmk/
python3 tools/dev.py build --board ../ulmk_boards/tc275_lite \
  --no-components --component silicon_stress

bash ../ulmk_boards/tc275_lite/scripts/hil-silicon-stress.sh \
  ../build/ulipe-tricore-tc275_lite/ulmk
```

ELF path note: `dev.py` writes under `../build/ulipe-<arch>-<board>/ulmk`, not `ulmk/build/`.
