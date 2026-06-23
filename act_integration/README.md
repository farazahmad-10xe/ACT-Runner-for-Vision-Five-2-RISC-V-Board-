# ACT Integration Layer

This directory contains the in-repo adapter surface for targeting the VF2
runner from an external ACT tree.

Files:

- `rvmodel.h`: minimal model header exposing the symbols and halt behavior
  expected by the runner
- `template/`: minimal ACT target skeleton with a make fragment and example env

Current contract:

- `tohost` is the test verdict handoff location
- `begin_signature` / `end_signature` delimit the signature region
- `begin_failure_scratch` / `end_failure_scratch` delimit a small scratch area
- `RVMODEL_HALT` writes `gp` to `tohost` and executes `ecall`

Mode behavior:

- M-mode tests return directly through the runner M trap path
- S/U-mode tests return through the delegated S trap path and are bridged back
  to the runner

This is intentionally minimal. It is designed to let an ACT tree target the
runner's existing load and result model without pretending to cover every
possible ACT environment hook.
