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
  --stop-after-cases <n>
                       Stop when persisted SD next_index reaches n
  --stop-on-suite-complete
                       Stop when SD progress reports next_index >= table_count
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
stop_after_cases=0
stop_on_suite_complete=0

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
    --stop-after-cases) stop_after_cases="${2:-}"; shift 2 ;;
    --stop-on-suite-complete) stop_on_suite_complete=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

mkdir -p "$(dirname "$log_path")"

cycle_pid=""
serial_reader_pid=""

# Only one harness may consume a tty.  Multiple readers split bytes between
# logs and make valid UART output look truncated, reordered, and interleaved.
serial_lock_name="$(basename "$serial_dev" | tr -c 'A-Za-z0-9_.-' '_')"
serial_lock_path="/tmp/act-uart-${serial_lock_name}.lock"
exec 9>"$serial_lock_path"
if ! flock -n 9; then
  echo "ERROR: another UART monitor already owns $serial_dev (lock: $serial_lock_path)" >&2
  exit 1
fi

cleanup() {
  trap - INT TERM EXIT
  if [[ -n "${cycle_pid:-}" ]]; then
    kill -- "-$cycle_pid" 2>/dev/null || true
    kill "$cycle_pid" 2>/dev/null || true
    wait "$cycle_pid" 2>/dev/null || true
    cycle_pid=""
  fi
  if [[ -n "${serial_reader_pid:-}" ]]; then
    kill "$serial_reader_pid" 2>/dev/null || true
    wait "$serial_reader_pid" 2>/dev/null || true
    serial_reader_pid=""
  fi
}

stop_monitor() {
  echo "[HOST] interrupt received; stopping UART monitor"
  cleanup
  exit 130
}

trap cleanup EXIT
trap stop_monitor INT TERM

if [[ -z "${TUYA_DEVICE_ID:-}" || -z "${TUYA_DEVICE_IP:-}" || -z "${TUYA_LOCAL_KEY:-}" ]]; then
  if [[ -f ./devices.json ]]; then
    while IFS='=' read -r key value; do
      case "$key" in
        TUYA_DEVICE_ID) export TUYA_DEVICE_ID="${TUYA_DEVICE_ID:-$value}" ;;
        TUYA_DEVICE_IP) export TUYA_DEVICE_IP="${TUYA_DEVICE_IP:-$value}" ;;
        TUYA_LOCAL_KEY) export TUYA_LOCAL_KEY="${TUYA_LOCAL_KEY:-$value}" ;;
        TUYA_VERSION) export TUYA_VERSION="${TUYA_VERSION:-$value}" ;;
      esac
    done < <(python3 - <<'PY'
import json

with open("devices.json", "r", encoding="utf-8") as f:
    devices = json.load(f)

device = devices[0] if isinstance(devices, list) and devices else {}
fields = {
    "TUYA_DEVICE_ID": device.get("id", ""),
    "TUYA_DEVICE_IP": device.get("ip", ""),
    "TUYA_LOCAL_KEY": device.get("key", ""),
    "TUYA_VERSION": str(device.get("version", "3.4")),
}
for key, value in fields.items():
    if value:
        print(f"{key}={value}")
PY
)
  fi
fi

if [[ -z "${TUYA_DEVICE_ID:-}" || -z "${TUYA_DEVICE_IP:-}" || -z "${TUYA_LOCAL_KEY:-}" ]]; then
  echo "[HOST] WARN: Tuya credentials are incomplete; power cycling will fail unless --start-cycle is omitted or env vars are exported." >&2
fi

last_cycle_ts=0
last_boot_cycle_ts=0
consecutive_boot_fails=0
case_reports_seen=0
table_count=0
reset_cycle_pending=0

run_cycle_command() {
  if command -v setsid >/dev/null 2>&1; then
    setsid python3 "$ctl_path" cycle --delay "$cycle_delay" &
  else
    python3 "$ctl_path" cycle --delay "$cycle_delay" &
  fi
  cycle_pid="$!"
  wait "$cycle_pid"
  local rc="$?"
  cycle_pid=""
  return "$rc"
}

trigger_cycle() {
  local reason="$1"
  local now
  now="$(date +%s)"
  if [[ "$reason" != "watchdog_reset" ]] && (( now - last_cycle_ts < cooldown_sec )); then
    echo "[HOST] skip cycle (cooldown) reason=$reason"
    return 0
  fi
  echo "[HOST] hard cycle reason=$reason"
  if run_cycle_command; then
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
  if run_cycle_command; then
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

monitor_complete=0
while (( monitor_complete == 0 )); do
  while [[ ! -e "$serial_dev" ]]; do
    echo "[HOST] waiting for UART device: $serial_dev"
    sleep 1
  done
  if ! stty -F "$serial_dev" 115200 cs8 -cstopb -parenb -ixon -ixoff -icanon -echo raw; then
    echo "[HOST] UART exists but is not ready; retrying $serial_dev"
    sleep 1
    continue
  fi

  # Drain the UART into the raw log with cat.  The shell parser below may do
  # regex matching and power-control work, so it must not sit directly on the
  # small tty receive buffer or verbose signature dumps can lose bytes.
  log_offset=$(stat -c %s "$log_path" 2>/dev/null || echo 0)
  cat "$serial_dev" >> "$log_path" &
  serial_reader_pid=$!

  # Follow only bytes appended by this reader.  Falling behind here delays
  # reactions but no longer drops UART input because cat keeps draining it.
  set +e
  while IFS= read -r line; do
  echo "$line"

  # Any sign of successful boot progress resets boot-failure retry streak.
  if [[ "$line" == *"U-Boot SPL"* ]] || [[ "$line" == *"[CASE] START"* ]] || [[ "$line" == *"[SUITE] loaded external pack from SD tail"* ]]; then
    consecutive_boot_fails=0
    reset_cycle_pending=0
  fi

  if [[ "$line" =~ \[SD\]\ table_count=([0-9]+) ]]; then
    table_count="${BASH_REMATCH[1]}"
  fi

  if [[ "$line" == *"[CASE] REPORT name="* ]]; then
    case_reports_seen=$((case_reports_seen + 1))
  fi

  if [[ "$line" =~ \[SD\]\ monitor\ persist\ next_index=([0-9]+) ]]; then
    next_index="${BASH_REMATCH[1]}"
    if (( stop_on_suite_complete == 1 && table_count > 0 && next_index >= table_count )); then
      echo "[HOST] suite complete next_index=${next_index} table_count=${table_count}; stopping UART monitor"
      monitor_complete=1
      break
    fi
    # A failing or reset-prone case may emit more than one REPORT while the
    # SD retry state still points at the same table entry.  Stop according to
    # persisted pack progress, not the raw number of REPORT lines.
    if (( stop_after_cases > 0 && next_index >= stop_after_cases )); then
      echo "[HOST] stop-after-cases reached next_index=${next_index} case_reports=${case_reports_seen}; stopping UART monitor"
      monitor_complete=1
      break
    fi
  fi

  # Match the stable message body too.  A UART overrun may lose the short
  # "[RST] " prefix, and some boards print "watchdog armed" immediately
  # before relying on the host-side hard-cycle fallback.
  if [[ "$line" == *"fast fail reset"* ]] || \
     [[ "$line" == *"trigger watchdog reset"* ]] || \
     [[ "$line" == *"watchdog armed"* ]]; then
    if (( reset_cycle_pending == 0 )); then
      reset_cycle_pending=1
      trigger_cycle "watchdog_reset"
    else
      echo "[HOST] reset cycle already issued; ignoring duplicate reset marker"
    fi
    continue
  fi

  if [[ "$line" == *"BOOT fail,Error is 0xffffffff"* ]] || [[ "$line" == *"dwmci_s: Response Timeout."* ]] || [[ "$line" == *"ci_s: Response Timeout."* ]]; then
    trigger_boot_recovery_cycle
    continue
  fi
  done < <(tail -c "+$((log_offset + 1))" -f --pid="$serial_reader_pid" "$log_path")
  read_rc=$?
  if (( monitor_complete != 0 )); then
    kill "$serial_reader_pid" 2>/dev/null || true
  fi
  wait "$serial_reader_pid"
  reader_rc=$?
  serial_reader_pid=""
  set -e

  if (( monitor_complete == 0 )); then
    echo "[HOST] UART disconnected or reached EOF (read_rc=$read_rc reader_rc=$reader_rc); reopening $serial_dev"
    sleep 1
  fi
done
