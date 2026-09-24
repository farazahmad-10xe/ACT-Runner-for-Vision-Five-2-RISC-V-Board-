# Jenkins UART jobs

The repository defines five UART-only jobs:

- `vf2-uart-sanity`
- `vf2-uart-weekly`
- `bpif3-uart-sanity`
- `bpif3-uart-weekly`
- `riscv-uart-single-elf`

All jobs fetch the same runner branch, validate the selected board identity and
installed runner build, serialize access with a board-specific lock, power
cycle through the configured smart outlet, and publish results to the portal.
No Jenkins job writes boot media or builds an SD payload pack.

Installed firmware revisions are tracked in `ci/runner_inventory.json`.
Override `RUNNER_REVISION_OVERRIDE` only for an intentional reproducible build;
override `INSTALLED_RUNNER_BUILD` only while diagnosing inventory drift.

Install or update a job with:

```bash
python3 ci/jenkins/install_apollo_uart_job.py --user admin --job <job-name>
```
