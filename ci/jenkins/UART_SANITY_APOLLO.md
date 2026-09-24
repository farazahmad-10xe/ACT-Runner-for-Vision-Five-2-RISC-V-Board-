# Apollo VF2 UART-stream sanity job

The `vf2-uart-sanity` Pipeline runs on the Apollo controller's
`riscv-hw-agent` node. The agent PC owns the VF2 UART adapter, smart-plug
connection, toolchains, and persistent workspace. The SD card only contains
the UART-stream runner; Jenkins power-cycles the board and sends each newly
generated ELF over UART without rewriting the card.

`INSTALLED_RUNNER_BUILD` identifies the runner image currently on that card.
It is intentionally independent of the pipeline checkout revision, so a
documentation or CI-only commit does not require reflashing. The host refuses
to send an ELF if the board's READY banner advertises a different build ID.
Update this parameter only when a newly built runner image has actually been
flashed.

After execution, `prepare_uart_portal_results.py` converts the batch
`summary.json` into the shared `cases.json`, `cases.csv`, `summary.md`, and
per-case UART-log layout. Jenkins archives those files, publishes JUnit, and
uses the agent's `publish_jenkins_results.py` helper to post the run to the
Apollo results portal. Each successfully published Jenkins build displays a
`Portal results` link in its build description and prints the same URL in the
console log.

The controller must provide a secret-text credential named
`riscv-portal-ingest-token` (or the build parameter must name an equivalent
credential). The agent requires `/dev/ttyUSB0`, its existing `devices.json`,
the portal CA certificate, and the portal publishing helper.

Install or update only this job without placing an administrator token in the
shell history:

```sh
python3 ci/jenkins/install_apollo_uart_job.py --user YOUR_APOLLO_USERNAME
```

The installer prompts for an Apollo Jenkins API token, renders the committed
Pipeline into the job XML, and creates or updates only `vf2-uart-sanity`.

## Weekly UART regression

`vf2-uart-weekly` provides a `TEST_SCOPE` build parameter. Its default `all`
value generates privileged, non-privileged, and vector tests; `priv` generates
only the privileged suite. It runs Sail, optionally runs Spike, and streams
every Sail-runnable ELF from the selected scope to the same persistent SD
runner. The selected scope is recorded in the portal payload. The two UART
jobs share the `vf2-hardware` lock, so they cannot operate the board
concurrently. The weekly job is scheduled once each Sunday with the default
`all` scope and can also be started manually with either selection.

Install or update it with:

```sh
python3 ci/jenkins/install_apollo_uart_job.py \
  --user YOUR_APOLLO_USERNAME \
  --job vf2-uart-weekly
```
