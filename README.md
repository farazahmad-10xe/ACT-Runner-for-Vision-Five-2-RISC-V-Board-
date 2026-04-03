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
- `act_runtime.c`: SD pack loading, monitor, trap, runner flow
- `runner_shared.h`: shared constants, types, globals, interfaces
- `Makefile.act`: firmware and pack build entrypoint
- `build_act_pack.py`: pack builder for external ELF lists
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

## Build The Runner Firmware

```bash
cd /path/to/vf2_mmode_fw

make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
```

This produces:

- `firmware_act.elf`
- `firmware_act.bin`
- `firmware_act.dis`
- `uboot_part2_new.bin`

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
