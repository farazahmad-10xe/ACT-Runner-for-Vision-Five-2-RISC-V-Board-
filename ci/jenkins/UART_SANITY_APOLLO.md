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
Apollo results portal.

The controller must provide a secret-text credential named
`results-portal-ingest-token` (or the build parameter must name an equivalent
credential). The agent requires `/dev/ttyUSB0`, its existing `devices.json`,
the portal CA certificate, and the portal publishing helper.
