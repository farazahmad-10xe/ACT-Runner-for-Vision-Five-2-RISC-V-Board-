#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-$HOME/riscv-arch-test/work/visionfive2-rv64gc/elfs/rv64i}"
SD_DEV="${2:-/dev/sda}"
SERIAL_DEV="${3:-/dev/ttyUSB0}"
CAPTURE_SECONDS="${CAPTURE_SECONDS:-900}"
RUN_PRIV="${RUN_PRIV:-M}"
RUNNER_DELEG_POLICY="${RUNNER_DELEG_POLICY:-DELEGATION_POLICY_NONE}"
RUNNER_TOUCH_MENVCFG="${RUNNER_TOUCH_MENVCFG:-0}"
LOWER_PRIV_ALLOWLIST="${LOWER_PRIV_ALLOWLIST:-}"
MAX_PER_EXTENSION="${MAX_PER_EXTENSION:-0}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "$REPO_DIR"
mkdir -p logs reports ext_lists

if ! [[ "$MAX_PER_EXTENSION" =~ ^[0-9]+$ ]]; then
  echo "ERROR: MAX_PER_EXTENSION must be a non-negative integer." >&2
  exit 1
fi

# Build/flash runner once (external pack mode)
make -f Makefile.act clean
make -f Makefile.act EXTERNAL_ONLY=1 \
  RUN_PRIV="$RUN_PRIV" \
  RUNNER_DELEG_POLICY="$RUNNER_DELEG_POLICY" \
  RUNNER_TOUCH_MENVCFG="$RUNNER_TOUCH_MENVCFG"
mkimage -f vf2_act4.its uboot_part2_new.bin
truncate -s 4M uboot_part2_new.bin
./vf2_act_flash.sh --image ./uboot_part2_new.bin --sd-dev "$SD_DEV"

stty -F "$SERIAL_DEV" 115200 cs8 -cstopb -parenb -ixon -ixoff -icanon -echo raw

for extdir in "$BASE"/*; do
  [[ -d "$extdir" ]] || continue
  ext="$(basename "$extdir")"

  if [[ "$MAX_PER_EXTENSION" -gt 0 ]]; then
    find "$extdir" -maxdepth 1 -type f -name '*.elf' | sort | awk -v n="$MAX_PER_EXTENSION" 'NR <= n' > "ext_lists/${ext}.list"
  else
    find "$extdir" -maxdepth 1 -type f -name '*.elf' | sort > "ext_lists/${ext}.list"
  fi
  if [[ ! -s "ext_lists/${ext}.list" ]]; then
    echo "SKIP $ext (no ELF)"
    continue
  fi
  if [[ "$MAX_PER_EXTENSION" -gt 0 ]]; then
    echo "Selected $(wc -l < "ext_lists/${ext}.list")/$MAX_PER_EXTENSION tests for $ext"
  fi

  gated_list="ext_lists/${ext}.${RUN_PRIV}.allowed.list"
  blocked_list="ext_lists/${ext}.${RUN_PRIV}.blocked.list"
  gating_report="reports/${ext}_gating_${RUN_PRIV}.csv"
  gate_args=(
    --test-list "ext_lists/${ext}.list"
    --run-priv "$RUN_PRIV"
    --out-allowed "$gated_list"
    --out-blocked "$blocked_list"
    --out-report "$gating_report"
  )
  if [[ "$RUN_PRIV" != "M" && -n "$LOWER_PRIV_ALLOWLIST" ]]; then
    gate_args+=(--allowlist "$LOWER_PRIV_ALLOWLIST")
  fi
  python3 ./gate_act_suite.py "${gate_args[@]}"
  if [[ ! -s "$gated_list" ]]; then
    echo "SKIP $ext (no gated tests for RUN_PRIV=$RUN_PRIV)"
    continue
  fi

  echo "===== Running extension: $ext (RUN_PRIV=$RUN_PRIV) ====="
  make -f Makefile.act act_pack.bin ACT_LIST="$gated_list"
  ./write_pack_to_sd_tail.sh ./act_pack.bin "$SD_DEV"

  # Start suite
  python3 ./tuya_plug_ctl.py cycle --delay 4

  log="logs/${ext}_uart.log"
  SERIAL_DEV_ENV="$SERIAL_DEV" REPO_DIR_ENV="$REPO_DIR" timeout "${CAPTURE_SECONDS}s" bash -lc '
    cat "$SERIAL_DEV_ENV" | while IFS= read -r line; do
      echo "$line"
      if [[ "$line" == *"[RST] watchdog armed"* ]] || [[ "$line" == *"[RST] trigger watchdog reset"* ]]; then
        python3 "$REPO_DIR_ENV/tuya_plug_ctl.py" cycle --delay 4
      fi
    done
  ' | tee "$log" || true

  ./extract_act_report.sh "$log" "reports/${ext}_check"
done
