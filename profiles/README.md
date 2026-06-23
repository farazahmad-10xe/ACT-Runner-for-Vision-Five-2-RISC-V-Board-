# Runner Profiles

This directory contains conservative gating inputs for the VF2 runner.

Principle:

- `M` mode may run broad ACT ELF sets with the existing runner contract.
- `S` and `U` mode are not treated as broadly supported.
- Lower-privilege runs must use an explicit allowlist.

Current files:

- `rva23_su_scaffold.allow`: empty-by-default allowlist for lower-privilege ACT
  runs while the runner is still incomplete

Recommended flow:

1. Generate a raw ACT ELF list from your external tree.
2. Run `gate_act_suite.py` with `--run-priv S` or `--run-priv U`.
3. Use the produced allowed list to build packs.
4. Expand the allowlist only after tests are validated on hardware.

Promotion helper:

- `promote_act_allowlist.py` reads `per_case_report.csv` from a validated run
  and appends exact passing ELF basenames into an allowlist.
