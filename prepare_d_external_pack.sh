#!/usr/bin/env bash
set -euo pipefail

# Exact source path requested by user.
D_DIR="${1:-$HOME/riscv-arch-test/work/visionfive2-rv64gc/elfs/rv64i/D}"
OUT_LIST="${2:-./d_extension_tests.list}"

if [[ ! -d "$D_DIR" ]]; then
  echo "ERROR: D directory not found: $D_DIR" >&2
  exit 1
fi

# Include symlinked .elf entries too (no -type f).
find "$D_DIR" -maxdepth 1 -name '*.elf' | sort > "$OUT_LIST"
count="$(wc -l < "$OUT_LIST")"

if [[ "$count" -eq 0 ]]; then
  echo "ERROR: no .elf entries found under $D_DIR" >&2
  exit 1
fi

echo "D tests listed: $count"
echo "List file: $OUT_LIST"

# Build small runner that expects external pack at 0x88000000.
make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1

# Build external pack payload (not embedded into firmware).
make -f Makefile.act act_pack.bin ACT_LIST="$OUT_LIST"

# Build FIT image from small runner and force 4 MiB container.
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin

echo
ls -lh firmware_act.bin act_pack.bin uboot_part2_new.bin

echo
echo "Next steps:"
echo "1) Flash only uboot_part2_new.bin to SD boot slot (4MiB)."
echo "2) Write act_pack.bin to SD tail with footer metadata:"
echo "     ./write_pack_to_sd_tail.sh ./act_pack.bin /dev/sdX"
echo "3) Boot board once. Runner will load the pack from SD tail into DDR @ 0x88000000 automatically."
