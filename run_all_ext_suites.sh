#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-$HOME/riscv-arch-test/work/visionfive2-rv64gc/elfs/rv64i}"
SD_DEV="${2:-/dev/sda}"
SERIAL_DEV="${3:-/dev/ttyUSB0}"
CAPTURE_SECONDS="${CAPTURE_SECONDS:-900}"

cd /home/lpt-10xe/vf2_mmode_fw
mkdir -p logs reports ext_lists

# Build/flash runner once (external pack mode)
make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
./vf2_act_flash.sh --image ./uboot_part2_new.bin --sd-dev "$SD_DEV"

stty -F "$SERIAL_DEV" 115200 cs8 -cstopb -parenb -ixon -ixoff -icanon -echo raw

for extdir in "$BASE"/*; do
  [[ -d "$extdir" ]] || continue
  ext="$(basename "$extdir")"

  find "$extdir" -maxdepth 1 -type f -name '*.elf' | sort > "ext_lists/${ext}.list"
  if [[ ! -s "ext_lists/${ext}.list" ]]; then
    echo "SKIP $ext (no ELF)"
    continue
  fi

  echo "===== Running extension: $ext ====="
  make -f Makefile.act act_pack.bin ACT_LIST="ext_lists/${ext}.list"
  ./write_pack_to_sd_tail.sh ./act_pack.bin "$SD_DEV"

  # Start suite
  python3 ./tuya_plug_ctl.py cycle --delay 4

  log="logs/${ext}_uart.log"
  timeout "${CAPTURE_SECONDS}s" bash -lc '
    cat "'"$SERIAL_DEV"'" | while IFS= read -r line; do
      echo "$line"
      if [[ "$line" == *"[RST] watchdog armed"* ]] || [[ "$line" == *"[RST] trigger watchdog reset"* ]]; then
        python3 /home/lpt-10xe/vf2_mmode_fw/tuya_plug_ctl.py cycle --delay 4
      fi
    done
  ' | tee "$log" || true

  ./extract_act_report.sh "$log" "reports/${ext}_check"
done
