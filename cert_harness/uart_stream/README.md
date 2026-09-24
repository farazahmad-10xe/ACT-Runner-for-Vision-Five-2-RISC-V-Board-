# UART-stream one-ELF transport

This optional transport sends one ELF from the host to the runner over the
board's existing bidirectional UART. It does not change the default
`sd_tail_pack` build or workflow.

## Scope and safety

- One trusted ELF per boot.
- VF2/JH7110 is validated on-board with single-ELF and power-cycled multi-ELF
  runs.
- BPI-F3/K1 has a separate OpenSBI FIT runner image and uses its existing
  NS16550-compatible UART at `0xd4017000`. The firmware build and host protocol
  are validated; on-board validation is the next step after flashing.
- Both board presets bound the receive buffer to 128 MiB. VF2 stages at
  `0x88000000`; BPI-F3 stages at `0x0c200000`.
- The target verifies the frame version, size, CRC-32, ELF load ranges, runner
  memory exclusion, and staging-buffer exclusion before execution.
- The `READY` record identifies the board and runner build. Host tools can
  reject an image built for a different board before transmitting an ELF.

## Board portability

The host framing protocol is board-independent, and a different boot flow does
not change it after the runner reaches `READY`. Each board still needs:

- a bidirectional UART backend compatible with the runner's RX/TX registers
  (the current implementation uses the existing 16550-style UART definitions),
- correct RAM and staging-buffer addresses,
- a board-specific way to package and boot the runner, and
- a reset path between ELFs for this one-ELF proof of concept.

Consequently, this can be ported to boards with other boot processes, but it is
not automatically compatible with every board that merely exposes a UART.

The protocol header is 88 bytes, little-endian:

```text
u32 magic       = 0x31534655 (bytes "UFS1")
u32 version     = 1
u64 elf_size
u32 crc32       = standard CRC-32 of ELF bytes
u32 name_len
u8  name[64]
```

The ELF bytes immediately follow the header after the target emits
`[UART_STREAM] HEADER_OK`.

## Build without changing the default setup

```bash
PATH=/home/lpt-10xe/riscv64/bin:$PATH \
bash cert_harness/tools/build_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_PRIV_M_OWN_ENV \
  --payload-transport uart_stream
```

The separate artifacts are written to:

```text
cert_harness/build/vf2_jh7110/ACT_PRIV_M_OWN_ENV/uart_stream/
```

Flash this UART-stream runner once:

```bash
sudo ./vf2_act_flash.sh \
  --image cert_harness/build/vf2_jh7110/ACT_PRIV_M_OWN_ENV/uart_stream/boot_image.bin \
  --sd-dev /dev/sda
```

Do not write an SD-tail pack for this transport.

### Build and flash BPI-F3/K1

```bash
PATH=/home/lpt-10xe/riscv64/bin:$PATH \
RUNNER_BUILD_ID_OVERRIDE=bpif3-uart-poc-v1 \
bash cert_harness/tools/build_profile.sh \
  --board bpif3_k1 \
  --profile ACT_PRIV_M_OWN_ENV \
  --payload-transport uart_stream
```

The BPI-F3 artifacts are isolated from the SD-pack build:

```text
cert_harness/build/bpif3_k1/ACT_PRIV_M_OWN_ENV/uart_stream/
```

Flash its OpenSBI partition once while the BPI-F3 SD card is attached to the
host:

```bash
sudo ./bpif3_act_flash.sh \
  --image cert_harness/build/bpif3_k1/ACT_PRIV_M_OWN_ENV/uart_stream/boot_image.bin \
  --sd-dev /dev/sda
```

Do not write an SD-tail pack. Return the card to the BPI-F3 before starting the
host sender.

## Send one ELF

Insert the SD card into the powered-off VF2, connect `/dev/ttyUSB0`, then run:

```bash
python3 cert_harness/uart_stream/send_elf.py \
  tests/standalone_invalid_pte_amo/invalid_pte_amo.elf \
  --serial-dev /dev/ttyUSB0 \
  --log logs/uart_stream_invalid_pte_amo.log
```

Power on the board after the host reports that it is waiting. The tool waits
for `READY`, sends the header, waits for `HEADER_OK`, sends the ELF, and records
the result through `DONE`.

For BPI-F3, use an ELF generated with the BPI-F3 ACT configuration and require
the target identity:

```bash
python3 cert_harness/uart_stream/send_elf.py \
  path/to/bpif3-test.elf \
  --serial-dev /dev/ttyUSB0 \
  --expect-board bpif3_k1 \
  --expect-runner-build bpif3-uart-poc-v1 \
  --log logs/bpif3_uart_stream.log
```

Validate a frame without hardware:

```bash
python3 cert_harness/uart_stream/send_elf.py \
  tests/standalone_invalid_pte_amo/invalid_pte_amo.elf \
  --dry-run
```

## Returning to the existing SD-tail setup

Rebuild and flash normally, without the transport override:

```bash
PATH=/home/lpt-10xe/riscv64/bin:$PATH \
bash cert_harness/tools/build_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_PRIV_M_OWN_ENV
```

All existing SD-tail source paths and commands remain available.

## Run multiple ELFs with Wi-Fi power isolation

The batch runner keeps the same UART runner image on SD, performs a Tuya outlet
power cycle before each case, sends one ELF, and stores a separate UART log plus
`summary.json`. It reads the first entry in the ignored local `devices.json`
unless Tuya environment variables or `--device-name` select another device.

Validate a batch without changing power or opening UART:

```bash
python3 cert_harness/uart_stream/run_elf_batch.py \
  tests/standalone_invalid_pte_amo/invalid_pte_amo.elf \
  tests/standalone_load_store_priority/load_store_priority.elf \
  --dry-run
```

Run the two-case VF2 batch:

```bash
python3 cert_harness/uart_stream/run_elf_batch.py \
  tests/standalone_invalid_pte_amo/invalid_pte_amo.elf \
  tests/standalone_load_store_priority/load_store_priority.elf \
  --serial-dev /dev/ttyUSB0 \
  --run-dir logs/runs/uart_stream_two_case_vf2
```

For BPI-F3, add `--expect-board bpif3_k1` and select the BPI-F3 smart outlet
with `--device-name` when more than one board is present in `devices.json`.

Use `--no-power-cycle` to prompt for a manual reset before every ELF. A batch
continues through target `FAIL` or `TIMEOUT` results, but stops on a UART or
power-control error unless `--keep-going` is specified.

The batch exits nonzero if any target case fails, times out, reports an error,
or does not complete. It writes both `summary.json` and Jenkins-compatible
`junit.xml`, and turns the smart outlet off on exit unless
`--leave-power-on` is specified.

## Jenkins UART sanity job

`Jenkinsfile.uart-sanity` defines a separate five-test pilot job. It leaves the
existing SD-pack sanity and weekly jobs unchanged. The SD card remains in the
VF2 and contains the UART runner; there is no per-build SD flashing.

Before the first run after a runner change, flash the UART `boot_image.bin`
once. The job compares the build ID reported in `READY` with the exact runner
commit used by Jenkins and rejects a stale installed image.

Install or update the job definitions after installing the `junit` and
`lockable-resources` plugins listed in `ci/jenkins/plugins.txt`. The hardware
stage takes the shared `vf2-hardware` lock so UART and SD-pack jobs can be
migrated to common hardware serialization without changing this transport.
