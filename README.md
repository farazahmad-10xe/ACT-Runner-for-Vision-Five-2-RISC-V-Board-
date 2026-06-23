# VF2 ACT Runner

Bare-metal ACT test runner, pack builder, UART automation, and report extraction for VisionFive2-class RISC-V boards.

## What This Repo Contains

- Runner firmware that boots in M-mode and executes ACT ELFs on hardware.
- External pack generation for large ACT test batches.
- UART-based execution logging and reboot automation.
- Host-side report extraction and signature comparison.

Key files:

- `main_act.c`: top-level firmware entry and orchestration
- `act_core.c`: platform, UART/debug, ELF parsing/loading
- `runner_lower_env.c`: lower-privilege machine environment setup and Sv39/PMP config
- `runner_smode.c`: S-mode entry and supervisor trap handling
- `runner_umode.c`: U-mode staging and S-to-U trampoline entry
- `runner_machine_trap.c`: machine trap entry and SBI-facing ECALL handling
- `runner_monitor.c`: hart2 monitor, case reporting, timeout/reset orchestration
- `runner_sd.c`: SD tail pack attach/load and external pack execution
- `act_runtime.c`: per-test execution flow and embedded pack handling
- `runner_shared.h`: shared constants, types, globals, interfaces
- `Makefile.act`: firmware and pack build entrypoint
- `build_act_pack.py`: pack builder for external ELF lists
- `send_elf_over_uart.py`: host-side single-ELF UART sender
- `gate_act_suite.py`: conservative ACT suite gating for `M` vs `S/U` runs
- `profiles/`: allowlists for lower-privilege runner profiles
- `act_integration/rvmodel.h`: minimal ACT adapter header for this runner
- `install_act_integration.sh`: helper to install the adapter into an ACT tree
- `serial_auto_rebooter.sh`: UART capture and power-cycle automation
- `extract_act_report.sh`: host-side report extraction

## Architecture

The firmware runs in M-mode and uses two harts:

- hart 1: loads and executes the test ELF
- hart 2: monitors `.tohost`, dumps signature/failure-scratch regions, persists suite progress, and schedules reset

Tests can be supplied either as:

- one embedded ELF
- one embedded packed suite
- one external SD-tail pack

## Repository Safety

This repo contains local automation for a Tuya-controlled power plug and board-specific host setup. For safe publication:

- local device credentials are intentionally ignored by `.gitignore`
- use `devices.example.json` as the template instead of committing your real `devices.json`
- do not commit `snapshot.json`, `tuya-raw.json`, or generated logs/reports

## Prerequisites

- `riscv64-unknown-elf-gcc`
- `riscv64-unknown-elf-objcopy`
- `riscv64-unknown-elf-objdump`
- `mkimage`
- Python 3
- ACT-generated ELF tree available locally
- SD card access on the host

Set these for your environment:

- `ACT_RV64I_ELF_BASE`: directory containing ACT ELFs, for example `.../work-vf2/visionfive2-rv64gc/elfs/rv64i`
- `ACT_GOLDEN_ROOT`: root used for host-side signature comparison
- `SD_DEV`: target SD block device, for example `/dev/sda`

Privilege scaffold:

- `RUN_PRIV=M`: current default runner behavior
- `RUN_PRIV=S`: lower-privilege supervisor execution path with narrow delegated trap handling
- `RUN_PRIV=U`: user execution path entered through an S-mode trampoline with delegated U traps returning through S
- `RUNNER_TOUCH_MENVCFG=0|1`: whether the runner should explicitly program `menvcfg` and `senvcfg`
- `RUNNER_DELEG_POLICY=DELEGATION_POLICY_NONE|DELEGATION_POLICY_S_BASE`: requested delegation profile
- `RUNNER_ASSUME_RVA23_PRIV_FEATURES=0|1`: when enabled, assume ratified supervisor-side envcfg features such as `Sstc`, `Svpbmt`, `Svadu`, and `Ssnpm`
- `RUNNER_UART_BOOT_TIMEOUT_MS=<n>`: if nonzero, wait briefly at boot for one raw ELF over UART before falling back to SD/embedded sources
- `RUNNER_UART_ONLY=0|1`: when enabled, require the UART ELF download and do not fall back to SD/embedded payloads
- `RUNNER_UART_DOWNLOAD_BUFFER_BYTES=<n>`: maximum raw ELF size accepted by the UART downloader, default `2097152u`

The lower-privilege execution path now targets a broad RVA23-style S/U execution
environment for ACT payloads. It currently provides:

- lower-privilege entry paths for both `S` and `U`
- delegated `U -> S -> M` and `S -> M` completion/trap return plumbing
- delegated contained traps for the common lower-privilege synchronous fault set
- delegated supervisor software/timer/external interrupts
- an Sv39 map derived from the actual runner image, payload segments, and execution stacks

Phase-2 baseline now also programs a machine-env profile for `medeleg`,
`mideleg`, `mie`, `mcounteren`, `mcountinhibit`, `menvcfg`, `scounteren`,
`senvcfg`, `sie`, `satp`, and PMP. Lower-privilege runs expose counters through
`mcounteren/scounteren`, delegate supervisor count-overflow interrupts, tighten
page permissions around runner code/data/stacks and loaded payload segments, and
replace the old single broad PMP aperture with explicit runner/payload regions.

Important boundary: this runner can provide the execution-environment plumbing
required by the ratified `RVA23 Profiles, Version 1.0, 2024-10-17`, but it
cannot manufacture hardware support for mandatory ISA extensions that the SoC
does not implement. Full profile pass/fail therefore still depends on the
underlying core.

## Load One ELF Over UART

For bring-up or ad hoc profile runs, the runner can wait for one raw ELF from
the host over UART and then feed that blob into the normal ELF loader. In
UART-only mode, test selection happens on the host at boot time; the SD card only
needs the runner firmware image, not the test ELF or external pack.

Build the firmware once with a nonzero UART wait window and no embedded payload:

```bash
make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1 RUN_PRIV=S \
  RUNNER_DELEG_POLICY=DELEGATION_POLICY_S_BASE \
  RUNNER_UART_BOOT_TIMEOUT_MS=30000 \
  RUNNER_UART_ONLY=1
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
```

On the host, use `pyserial` and send the ELF after the board prints
`[UARTDL] READY`:

```bash
python3 ./send_elf_over_uart.py \
  --serial-dev /dev/ttyUSB0 \
  --elf /path/to/test.elf
```

The UART protocol is intentionally simple:

- target prints a `READY` banner with the transfer limits
- host sends a little-endian 20-byte header containing:
  - `magic`
  - `version`
  - `payload_size`
  - `payload_crc32`
  - `reserved`
- host then sends the raw ELF bytes
- target verifies size and CRC32, then runs the normal `load_elf_blob()` path

If `RUNNER_UART_ONLY=1` and no valid UART payload arrives before the timeout,
the runner reports an error and waits. With `RUNNER_UART_ONLY=0`, it falls back
to the existing SD/external/embedded flow.

To run every ELF under an ACT ELF tree from the host in S-mode, use the
UART-tree runner:

```bash
./run_uart_elf_tree.sh \
  --elf-root ~/riscv-arch-test/work-vf2/visionfive2-rv64gc/elfs/rv64i \
  --serial-dev /dev/ttyUSB0 \
  --flash-sd /dev/sda
```

This builds one `RUN_PRIV=S` UART-only firmware image, flashes that image once
when `--flash-sd` is provided, then streams each ELF from the host over UART.
Because the target executes one UART-downloaded ELF per boot, provide
`--hard-boot-cmd '<command>'` for automatic power cycling or reset the board
manually at each prompt.

## Build The Runner Firmware

```bash
cd /path/to/vf2_mmode_fw

make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1 RUN_PRIV=M
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
```

This produces:

- `firmware_act.elf`
- `firmware_act.bin`
- `firmware_act.dis`
- `uboot_part2_new.bin`

## Install The ACT Adapter

If you want an external ACT tree to target this runner explicitly, install the
in-repo adapter header into the target include directory used by your ACT
build:

```bash
cd /path/to/vf2_mmode_fw

./install_act_integration.sh /path/to/act-target/include
```

This installs `act_integration/rvmodel.h` as `rvmodel.h` in the destination.

If you also want a small target skeleton with a make fragment and example
environment file:

```bash
cd /path/to/vf2_mmode_fw

./install_act_integration.sh /path/to/act-target/include --with-template
```

That creates `vf2_runner_target/` under the destination alongside the installed
header.

## Gate ACT Lists By Runner Profile

Use `gate_act_suite.py` to classify ACT ELFs before claiming they are runnable
under a given execution profile.

`M` mode is always broadly allowed. `S/U` mode now also allows the full list by
default; an allowlist is optional when you want to stage lower-privilege bring-up
conservatively.

Example:

```bash
cd /path/to/vf2_mmode_fw

python3 ./gate_act_suite.py \
  --test-list ext_lists/ALL.list \
  --run-priv S \
  --allowlist profiles/rva23_su_scaffold.allow \
  --out-allowed ext_lists/ALL_S_allowed.list \
  --out-blocked ext_lists/ALL_S_blocked.list \
  --out-report reports/ALL_S_gating.csv
```

Only the generated allowed list should be used for pack builds. If you omit
`--allowlist` for `S/U`, that allowed list is simply the full raw list.

The bulk external-suite runner also honors this model:

```bash
RUN_PRIV=S \
RUNNER_DELEG_POLICY=DELEGATION_POLICY_S_BASE \
./run_all_ext_suites.sh /path/to/riscv-arch-test/work-vf2/visionfive2-rv64gc/elfs/rv64i
```

For `RUN_PRIV=S` or `RUN_PRIV=U`, `run_all_ext_suites.sh` now gates each raw
extension list before building the external pack. If you set
`LOWER_PRIV_ALLOWLIST`, it narrows the lower-privilege run. If no tests survive
gating, that extension is skipped.

## Promote Validated Lower-Privilege Tests

After a real lower-privilege run passes on hardware, promote those exact cases
into the allowlist:

```bash
cd /path/to/vf2_mmode_fw

python3 ./promote_act_allowlist.py \
  --per-case-csv reports/some_run/per_case_report.csv \
  --allowlist profiles/rva23_su_scaffold.allow
```

This appends exact `*.elf` basenames for passing cases. It does not widen to
globs automatically.

## Build A Full External ACT Pack

```bash
cd /path/to/vf2_mmode_fw

ACT_RV64I_ELF_BASE=/path/to/riscv-arch-test/work-vf2/visionfive2-rv64gc/elfs/rv64i

mkdir -p ext_lists
find "$ACT_RV64I_ELF_BASE" -mindepth 2 -maxdepth 2 -name '*.elf' | sort > ext_lists/ALL.list

make -f Makefile.act act_pack.bin ACT_LIST=ext_lists/ALL.list
```

## Flash Runner And Write Pack To SD

```bash
cd /path/to/vf2_mmode_fw

SD_DEV=/dev/sda

./vf2_act_flash.sh --image ./uboot_part2_new.bin --sd-dev "$SD_DEV"
./write_pack_to_sd_tail.sh ./act_pack.bin "$SD_DEV"
sync
```

## One Full End-To-End Command Set

```bash
cd /path/to/vf2_mmode_fw

SD_DEV=/dev/sda
ACT_RV64I_ELF_BASE=/path/to/riscv-arch-test/work-vf2/visionfive2-rv64gc/elfs/rv64i

mkdir -p ext_lists
find "$ACT_RV64I_ELF_BASE" -mindepth 2 -maxdepth 2 -name '*.elf' | sort > ext_lists/ALL.list

make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
make -f Makefile.act act_pack.bin ACT_LIST=ext_lists/ALL.list

./vf2_act_flash.sh --image ./uboot_part2_new.bin --sd-dev "$SD_DEV"
./write_pack_to_sd_tail.sh ./act_pack.bin "$SD_DEV"
sync
```

## UART Capture And Automated Reboot

`serial_auto_rebooter.sh` expects Tuya device information via environment variables:

- `TUYA_DEVICE_ID`
- `TUYA_DEVICE_IP`
- `TUYA_LOCAL_KEY`
- `TUYA_VERSION`

Example:

```bash
cd /path/to/vf2_mmode_fw

export TUYA_DEVICE_ID='your-device-id'
export TUYA_DEVICE_IP='192.168.1.100'
export TUYA_LOCAL_KEY='your-local-key'
export TUYA_VERSION='3.4'

./serial_auto_rebooter.sh \
  --serial-dev /dev/ttyUSB0 \
  --log "./logs/run_$(date +%F_%H%M%S).log" \
  --start-cycle \
  --cooldown 20 \
  --boot-cooldown 5 \
  --boot-retries 10 \
  --boot-backoff 45 \
  --cycle-delay 12
```

## Report Extraction

```bash
cd /path/to/vf2_mmode_fw

LATEST_LOG="$(ls -1t logs/*.log | head -n 1)"
ACT_GOLDEN_ROOT=/path/to/riscv-arch-test/work-vf2

EXPECTED_LIST=ext_lists/ALL.list \
GOLDEN_ROOT="$ACT_GOLDEN_ROOT" \
./extract_act_report.sh "$LATEST_LOG" reports/latest_check
```

Useful outputs:

- `reports/latest_check/suite_summary.txt`
- `reports/latest_check/per_case_report.csv`
- `reports/latest_check/golden_compare.csv`

## Notes On ACT ELF Modes

There are two important ACT execution modes:

- `*.sig.elf`
  - pure signature-producing flow
  - best for external signature comparison on hardware

- final self-checking `*.elf`
  - uses embedded expected-result data
  - useful for diagnostics
  - can produce false failures on PC-sensitive tests like `auipc` if the final ELF layout differs from the signature-generation ELF

For hardware validation, pure signature flow is usually the safer oracle.

## Suggested Publish Flow

```bash
cd /path/to/vf2_mmode_fw

git init
git add .
git commit -m "Initial import of VF2 ACT runner and automation"
git branch -M main
git remote add origin git@github.com:YOUR_USERNAME/YOUR_REPO.git
git push -u origin main
```

Before pushing:

- verify that `devices.json`, `snapshot.json`, and `tuya-raw.json` are not staged
- verify that generated binaries, logs, and reports are not staged
