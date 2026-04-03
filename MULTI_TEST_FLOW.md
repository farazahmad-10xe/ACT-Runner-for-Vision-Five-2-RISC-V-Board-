# Multi-Test Single-Flash Flow

This firmware supports two build modes:

- Single test ELF (existing behavior):
  - `make -f Makefile.act ACT_ELF=/path/to/test.elf`
- Packed multi-test ELF list (new behavior):
  - Create a list file with one ELF path per line.
  - `make -f Makefile.act ACT_LIST=/path/to/d_tests.list`
- External-pack mode (recommended for large suites like D=74):
  - `make -f Makefile.act EXTERNAL_ONLY=1` (small runner in FIT)
  - `make -f Makefile.act act_pack.bin ACT_LIST=/path/to/d_tests.list`
  - Runner auto-loads `act_pack.bin` from SD raw tail into DDR at `0x88000000`.

## Example list file

```
/path/to/D-test-01.elf
/path/to/D-test-02.elf
# comments are allowed
/path/to/D-test-74.elf
```

## Flash once

After build:

```
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
./vf2_act_flash.sh --image uboot_part2_new.bin --sd-dev /dev/sda
```

Insert SD in board and boot once. The runner will iterate all packed tests.

## Exact D-extension preparation

Use:

```
./prepare_d_external_pack.sh ~/riscv-arch-test/work/visionfive2-rv64gc/elfs/rv64i/D \
  ./d_extension_tests.list
```

This creates:
- `d_extension_tests.list` (74 tests from that directory)
- `firmware_act.bin` (small runner)
- `uboot_part2_new.bin` (4MiB flashable image)
- `act_pack.bin` (external payload, ~34MiB)

Then write pack payload to SD tail:

```
./write_pack_to_sd_tail.sh ./act_pack.bin /dev/sdX
```

On boot, runner probes SD (`mmc1`), reads footer at last sector, loads the pack,
then runs all tests automatically.

## UART output markers

- `[CASE] START name=<test>`
- `[CASE] RESULT name=<test> status=PASS|FAIL tohost=0x...`
- `[SIG] ...` signature dump per test
- `[SUITE] SUMMARY total=<n> pass=<n> fail=<n>`

Use your existing `extract_act_report.sh` logic as reference, or add a suite parser for these markers.
