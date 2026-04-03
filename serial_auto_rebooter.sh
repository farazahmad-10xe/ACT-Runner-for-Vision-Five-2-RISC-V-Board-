#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./serial_auto_rebooter.sh [options]

Options:
  --serial-dev <path>   UART device (default: /dev/ttyUSB0)
  --log <path>          Output log file (default: ./logs/uart_auto.log)
  --cooldown <sec>      Min seconds between power cycles (default: 20)
  --boot-cooldown <sec> Min seconds between boot-failure recovery cycles (default: 6)
  --boot-retries <n>    Max consecutive boot-failure cycles before backoff (default: 8)
  --boot-backoff <sec>  Backoff wait after hitting boot-retry limit (default: 60)
  --cycle-delay <sec>   Off duration for adapter cycle (default: 8)
  --ctl <path>          Path to tuya_plug_ctl.py (default: ./tuya_plug_ctl.py)
  --start-cycle         Do one hard cycle before UART monitoring starts
  -h, --help            Show this help

Env required by tuya_plug_ctl.py:
  TUYA_DEVICE_ID, TUYA_DEVICE_IP, TUYA_LOCAL_KEY, TUYA_VERSION(optional)
EOF
}

serial_dev="/dev/ttyUSB0"
log_path="./logs/uart_auto.log"
cooldown_sec=20
boot_cooldown_sec=6
boot_retries=8
boot_backoff_sec=60
cycle_delay=8
ctl_path="./tuya_plug_ctl.py"
start_cycle=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial-dev) serial_dev="${2:-}"; shift 2 ;;
    --log) log_path="${2:-}"; shift 2 ;;
    --cooldown) cooldown_sec="${2:-}"; shift 2 ;;
    --boot-cooldown) boot_cooldown_sec="${2:-}"; shift 2 ;;
    --boot-retries) boot_retries="${2:-}"; shift 2 ;;
    --boot-backoff) boot_backoff_sec="${2:-}"; shift 2 ;;
    --cycle-delay) cycle_delay="${2:-}"; shift 2 ;;
    --ctl) ctl_path="${2:-}"; shift 2 ;;
    --start-cycle) start_cycle=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

mkdir -p "$(dirname "$log_path")"

stty -F "$serial_dev" 115200 cs8 -cstopb -parenb -ixon -ixoff -icanon -echo raw

last_cycle_ts=0
last_boot_cycle_ts=0
consecutive_boot_fails=0

trigger_cycle() {
  local reason="$1"
  local now
  now="$(date +%s)"
  if [[ "$reason" != "watchdog_reset" ]] && (( now - last_cycle_ts < cooldown_sec )); then
    echo "[HOST] skip cycle (cooldown) reason=$reason"
    return 0
  fi
  echo "[HOST] hard cycle reason=$reason"
  if python3 "$ctl_path" cycle --delay "$cycle_delay"; then
    last_cycle_ts="$now"
  else
    echo "[HOST] WARN: cycle command failed reason=$reason" >&2
  fi
}

trigger_boot_recovery_cycle() {
  local now
  now="$(date +%s)"
  if (( now - last_boot_cycle_ts < boot_cooldown_sec )); then
    echo "[HOST] skip cycle (boot cooldown) reason=boot_media_failure"
    return 0
  fi
  if (( consecutive_boot_fails >= boot_retries )); then
    echo "[HOST] boot retry limit reached (${boot_retries}), backing off ${boot_backoff_sec}s"
    sleep "$boot_backoff_sec"
    consecutive_boot_fails=0
  fi
  consecutive_boot_fails=$((consecutive_boot_fails + 1))
  echo "[HOST] hard cycle reason=boot_media_failure retry=${consecutive_boot_fails}/${boot_retries}"
  if python3 "$ctl_path" cycle --delay "$cycle_delay"; then
    last_boot_cycle_ts="$now"
    last_cycle_ts="$now"
  else
    echo "[HOST] WARN: cycle command failed reason=boot_media_failure" >&2
  fi
}

if [[ "$start_cycle" -eq 1 ]]; then
  echo "[HOST] startup hard cycle"
  trigger_cycle "startup"
fi

cat "$serial_dev" | while IFS= read -r line; do
  echo "$line"

  # Any sign of successful boot progress resets boot-failure retry streak.
  if [[ "$line" == *"U-Boot SPL"* ]] || [[ "$line" == *"[CASE] START"* ]] || [[ "$line" == *"[SUITE] loaded external pack from SD tail"* ]]; then
    consecutive_boot_fails=0
  fi

  if [[ "$line" == *"[RST] trigger watchdog reset"* ]]; then
    trigger_cycle "watchdog_reset"
    continue
  fi

  if [[ "$line" == *"BOOT fail,Error is 0xffffffff"* ]] || [[ "$line" == *"dwmci_s: Response Timeout."* ]]; then
    trigger_boot_recovery_cycle
    continue
  fi
done | tee "$log_path"
