# Reusable Certification Harness Sidecar

This directory is Phase 1 of the migration from a VF2-specific firmware tree to
a reusable certification harness. It intentionally does not move or rewrite the
existing firmware sources.

The harness separates three concerns:

- boards: SoC or board-specific packaging, flashing, UART, and power control
- profiles: execution environment settings such as payload type, privilege
  mode, delegation policy, and SBI compatibility policy
- runs: generated artifacts, logs, reports, and evidence manifests

Profiles are not scripts and should not contain board behavior.

## Build A Profile

```bash
bash cert_harness/tools/build_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_U_COMPAT \
  --act-list ext_lists/S_FDIM_2_random.S.allowed.list
```

Artifacts are written to:

```text
cert_harness/build/<board>/<profile>/<payload_transport>/
```

Each build directory contains:

```text
firmware.elf
firmware.bin
firmware.dis
uboot_part2_new.bin, when platform packaging is enabled
act_pack.bin, when ACT_LIST is provided
manifest.json
size.txt
sha256sums.txt
```

## Run A Profile

`tools/run_profile.sh` is the Phase 1 orchestration wrapper. It keeps run
evidence under `cert_harness/runs/`. Hardware-specific operations go through a
board adapter from `cert_harness/board_adapters/`.

Build only:

```bash
cert_harness/tools/run_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_U_COMPAT \
  --build \
  --act-list ext_lists/S_FDIM_2_random.S.allowed.list
```

Flash the sidecar artifacts to the SD card:

```bash
cert_harness/tools/run_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_U_COMPAT \
  --flash-image \
  --write-pack \
  --sd-dev /dev/sda
```

Capture UART and generate reports:

```bash
cert_harness/tools/run_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_U_COMPAT \
  --run-uart \
  --report \
  --serial-dev /dev/ttyUSB0 \
  --start-cycle
```

Generate a report from an existing log:

```bash
cert_harness/tools/run_profile.sh \
  --board vf2_jh7110 \
  --profile ACT_U_COMPAT \
  --report \
  --log logs/U_FDIM_2.log
```

## Available Profiles

Profile presets live in `cert_harness/profiles/`:

- `ACT_PRIV_M_OWN_ENV.env`
- `ACT_M_MIN.env`
- `ACT_S_COMPAT.env`
- `ACT_U_COMPAT.env`
- `ACT_TSBI_EXPERIMENTAL.env`
- `RIESCUE_M_MIN.env`

## Board Presets

Board presets live in `cert_harness/boards/`:

- `vf2_jh7110.env`

The current VF2 board preset still points at the existing VF2 image format and
toolchain. Its hardware operations are isolated in:

- `cert_harness/board_adapters/vf2_jh7110.sh`

To add a new SoC or board, add a board preset plus a board adapter. Do not add a
new profile unless the execution environment changes.

## Adding A Board

For a new board, create:

```text
cert_harness/boards/<board>.env
cert_harness/board_adapters/<board>.sh
```

The board env owns boot and memory mapping:

```bash
BOARD_LINKER_SCRIPT=cert_harness/boards/<board>/link.ld
BOARD_FW_LOAD_ADDR=0x...
BOARD_FW_STACK_BYTES=0x...
BOARD_RAM_BASE=0x...
BOARD_RAM_LIMIT=0x...
BOARD_UART_BASE=0x...
BOARD_CLINT_MSIP_BASE=0x...
BOARD_CLINT_MTIMECMP_BASE=0x...
BOARD_CLINT_MTIME_ADDR=0x...
BOARD_FIXED_TOHOST_ADDR=0x...
BOARD_EXT_PACK_ADDR=0x...
BOARD_SDIO1_BASE=0x...
BOARD_WDT_ENABLE=0
```

The board adapter owns hardware actions:

```bash
board_flash_image
board_write_pack
board_capture_uart
```

Then all profiles use the same command shape:

```bash
cert_harness/tools/build_profile.sh --board <board> --profile ACT_U_COMPAT
cert_harness/tools/run_profile.sh --board <board> --profile ACT_U_COMPAT
```

Boards without SD should set:

```bash
BOARD_PAYLOAD_TRANSPORT=embedded_pack
BOARD_SD_ENABLE=0
```

That links `act_pack.bin` into the firmware image and makes `--all` skip the
external pack write step.
