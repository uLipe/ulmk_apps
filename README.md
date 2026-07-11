# ulmk_apps

External components consumed by [ulmk](https://github.com/uLipe/ulmk) via the
sibling `../ulmk_apps` discovery path (`tools/dev.py` mounts it into the
container).

| Component | Purpose |
|-----------|---------|
| `silicon_baseline` | TC275 HIL: console + hello |
| `silicon_e2e` | TC275 HIL: public API smoke test (`SILICON_E2E: PASS`) |
