#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  cert_harness/tools/run_profile.sh --board <name> --profile <name> [steps] [options]

Steps:
  --build              Build profile artifacts first.
  --flash-image        Flash uboot_part2_new.bin to the SD boot slot.
  --write-pack         Write act_pack.bin to the SD tail footer area.
  --run-uart           Capture UART using serial_auto_rebooter.sh.
  --report             Generate report from the UART log.
  --all                Build, flash image, write pack, run UART, and report.
                       For embedded_pack boards, --all skips --write-pack.

Inputs:
  --act-list <file>    ACT ELF list for external pack builds.
  --act-elf <file>     Single ACT ELF for embedded builds.
  --riescue-elf <file> Single Riescue ELF for Riescue builds.
  --payload-transport <t>
                       Override board payload transport for this run.
  --artifact-dir <dir> Use an existing artifact directory.

Hardware/options:
  --sd-dev <dev>       SD block device. Defaults to board DEFAULT_SD_DEV.
  --serial-dev <dev>   UART device. Defaults to board DEFAULT_SERIAL_DEV.
  --log <file>         UART log path. Defaults under cert_harness/runs/.
  --report-dir <dir>   Report output path. Defaults beside the UART log.
  --run-dir <dir>      Run evidence directory. Defaults under cert_harness/runs/.
  --capture-timeout <sec>
                       Stop UART capture after this many seconds. Default: 900.
                       Use 0 to run until interrupted.
  --start-cycle        Ask serial_auto_rebooter.sh to power-cycle before capture.
  --cooldown <sec>     serial_auto_rebooter.sh cooldown. Default: 20.
  --boot-cooldown <sec>
                       Boot-failure recovery cooldown. Default: 5.
  --boot-retries <n>   Boot-failure retries before backoff. Default: 10.
  --boot-backoff <sec> Boot-failure backoff. Default: 45.
  --cycle-delay <sec>  Power-cycle off duration. Default: 12.

Examples:
  cert_harness/tools/run_profile.sh --board vf2_jh7110 --profile ACT_U_COMPAT \
    --build --act-list ext_lists/S_FDIM_2_random.S.allowed.list

  cert_harness/tools/run_profile.sh --board vf2_jh7110 --profile ACT_U_COMPAT \
    --flash-image --write-pack --sd-dev /dev/sda

  cert_harness/tools/run_profile.sh --board vf2_jh7110 --profile ACT_U_COMPAT \
    --run-uart --report --serial-dev /dev/ttyUSB0 --log logs/U_FDIM_2.log
EOF
}

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

board=""
profile=""
act_list=""
act_elf=""
riescue_elf=""
payload_transport_override=""
artifact_dir=""
sd_dev=""
serial_dev=""
log_path=""
report_dir=""
run_dir=""
capture_timeout=900
start_cycle=0
cooldown_sec=20
boot_cooldown_sec=5
boot_retries=10
boot_backoff_sec=45
cycle_delay=12

do_build=0
do_flash=0
do_write_pack=0
do_run_uart=0
do_report=0
all_requested=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --board) board="${2:-}"; shift 2 ;;
    --platform) board="${2:-}"; shift 2 ;;
    --profile) profile="${2:-}"; shift 2 ;;
    --act-list) act_list="${2:-}"; shift 2 ;;
    --act-elf) act_elf="${2:-}"; shift 2 ;;
    --riescue-elf) riescue_elf="${2:-}"; shift 2 ;;
    --payload-transport) payload_transport_override="${2:-}"; shift 2 ;;
    --artifact-dir) artifact_dir="${2:-}"; shift 2 ;;
    --sd-dev) sd_dev="${2:-}"; shift 2 ;;
    --serial-dev) serial_dev="${2:-}"; shift 2 ;;
    --log) log_path="${2:-}"; shift 2 ;;
    --report-dir) report_dir="${2:-}"; shift 2 ;;
    --run-dir) run_dir="${2:-}"; shift 2 ;;
    --capture-timeout) capture_timeout="${2:-}"; shift 2 ;;
    --start-cycle) start_cycle=1; shift ;;
    --cooldown) cooldown_sec="${2:-}"; shift 2 ;;
    --boot-cooldown) boot_cooldown_sec="${2:-}"; shift 2 ;;
    --boot-retries) boot_retries="${2:-}"; shift 2 ;;
    --boot-backoff) boot_backoff_sec="${2:-}"; shift 2 ;;
    --cycle-delay) cycle_delay="${2:-}"; shift 2 ;;
    --build) do_build=1; shift ;;
    --flash-image) do_flash=1; shift ;;
    --write-pack) do_write_pack=1; shift ;;
    --run-uart) do_run_uart=1; shift ;;
    --report) do_report=1; shift ;;
    --all)
      all_requested=1
      do_build=1
      do_flash=1
      do_write_pack=1
      do_run_uart=1
      do_report=1
      shift
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$board" || -z "$profile" ]]; then
  echo "ERROR: --board and --profile are required." >&2
  usage
  exit 1
fi

board_env="$repo_root/cert_harness/boards/${board}.env"
profile_env="$repo_root/cert_harness/profiles/${profile}.env"
if [[ ! -f "$board_env" ]]; then
  echo "ERROR: board not found: $board_env" >&2
  exit 1
fi
if [[ ! -f "$profile_env" ]]; then
  echo "ERROR: profile not found: $profile_env" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$board_env"
# shellcheck disable=SC1090
source "$profile_env"

adapter="${BOARD_ADAPTER:-$board}"
adapter_file="$repo_root/cert_harness/board_adapters/${adapter}.sh"
if [[ ! -f "$adapter_file" ]]; then
  echo "ERROR: board adapter not found: $adapter_file" >&2
  exit 1
fi
# shellcheck disable=SC1090
source "$adapter_file"

payload_transport="${payload_transport_override:-${BOARD_PAYLOAD_TRANSPORT:-sd_tail_pack}}"
case "$payload_transport" in
  sd_tail_pack|embedded_pack|uart_stream|jtag_load|bootloader_ram) ;;
  *) echo "ERROR: unsupported BOARD_PAYLOAD_TRANSPORT=$payload_transport" >&2; exit 1 ;;
esac
if [[ "$payload_transport" == "sd_tail_pack" ]]; then
  BOARD_SD_ENABLE=1
else
  BOARD_SD_ENABLE=0
fi
if [[ "$all_requested" -eq 1 && "$payload_transport" != "sd_tail_pack" ]]; then
  do_write_pack=0
fi

if [[ -z "$artifact_dir" ]]; then
  artifact_dir="$repo_root/cert_harness/build/$board/$profile/$payload_transport"
fi
if [[ -z "$sd_dev" ]]; then
  sd_dev="${DEFAULT_SD_DEV:-/dev/sda}"
fi
if [[ -z "$serial_dev" ]]; then
  serial_dev="${DEFAULT_SERIAL_DEV:-/dev/ttyUSB0}"
fi

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
if [[ -z "$run_dir" ]]; then
  run_dir="$repo_root/cert_harness/runs/$board/$profile/$timestamp"
fi
mkdir -p "$run_dir"

if [[ -z "$log_path" ]]; then
  log_path="$run_dir/uart.log"
fi
if [[ -z "$report_dir" ]]; then
  report_dir="$run_dir/report"
fi

if [[ "$do_build" -eq 1 ]]; then
  build_cmd=("$repo_root/cert_harness/tools/build_profile.sh" --board "$board" --profile "$profile")
  [[ -n "$act_list" ]] && build_cmd+=(--act-list "$act_list")
  [[ -n "$act_elf" ]] && build_cmd+=(--act-elf "$act_elf")
  [[ -n "$riescue_elf" ]] && build_cmd+=(--riescue-elf "$riescue_elf")
  [[ -n "$payload_transport_override" ]] && build_cmd+=(--payload-transport "$payload_transport_override")
  echo "==> Build profile: $board/$profile"
  (cd "$repo_root" && "${build_cmd[@]}")
fi

if [[ "$do_flash" -eq 1 ]]; then
  image="$artifact_dir/boot_image.bin"
  if [[ ! -f "$image" && -n "${BOARD_BOOT_IMAGE:-}" ]]; then
    image="$artifact_dir/$BOARD_BOOT_IMAGE"
  fi
  [[ -f "$image" ]] || { echo "ERROR: boot image not found: $image" >&2; exit 1; }
  echo "==> Flash boot image to $sd_dev"
  board_flash_image "$repo_root" "$image" "$sd_dev"
fi

if [[ "$do_write_pack" -eq 1 ]]; then
  if [[ "$payload_transport" != "sd_tail_pack" ]]; then
    echo "ERROR: --write-pack requires BOARD_PAYLOAD_TRANSPORT=sd_tail_pack, got $payload_transport" >&2
    exit 1
  fi
  if [[ "${BOARD_SD_ENABLE:-1}" == "0" ]]; then
    echo "ERROR: --write-pack requested but BOARD_SD_ENABLE=0" >&2
    exit 1
  fi
  pack="$artifact_dir/act_pack.bin"
  [[ -f "$pack" ]] || { echo "ERROR: ACT pack not found: $pack" >&2; exit 1; }
  echo "==> Write ACT pack to SD tail on $sd_dev"
  board_write_pack "$repo_root" "$pack" "$sd_dev"
fi

if [[ "$do_run_uart" -eq 1 ]]; then
  mkdir -p "$(dirname "$log_path")"
  echo "==> Capture UART: $serial_dev -> $log_path"
  if [[ "$capture_timeout" == "0" ]]; then
    board_capture_uart "$repo_root" "$serial_dev" "$log_path" "$cooldown_sec" \
      "$boot_cooldown_sec" "$boot_retries" "$boot_backoff_sec" \
      "$cycle_delay" "$start_cycle"
  else
    set +e
    timeout "$capture_timeout" bash -c \
      'source "$1"; board_capture_uart "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9" "${10}"' \
      bash "$adapter_file" "$repo_root" "$serial_dev" "$log_path" \
      "$cooldown_sec" "$boot_cooldown_sec" "$boot_retries" "$boot_backoff_sec" \
      "$cycle_delay" "$start_cycle"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 && "$rc" -ne 124 ]]; then
      echo "ERROR: UART capture failed with rc=$rc" >&2
      exit "$rc"
    fi
    [[ "$rc" -eq 124 ]] && echo "==> UART capture stopped after ${capture_timeout}s"
  fi
fi

if [[ "$do_report" -eq 1 ]]; then
  [[ -f "$log_path" ]] || { echo "ERROR: UART log not found: $log_path" >&2; exit 1; }
  echo "==> Generate report: $report_dir"
  (cd "$repo_root" && ./extract_act_report.sh "$log_path" "$report_dir")
fi

run_manifest="$run_dir/run_manifest.env"
{
  echo "RUN_TIMESTAMP_UTC=$timestamp"
  echo "BOARD=$board"
  echo "BOARD_ADAPTER=$adapter"
  echo "PAYLOAD_TRANSPORT=$payload_transport"
  echo "BOARD_SD_ENABLE=$BOARD_SD_ENABLE"
  echo "PROFILE=$profile"
  echo "ARTIFACT_DIR=$artifact_dir"
  echo "SD_DEV=$sd_dev"
  echo "SERIAL_DEV=$serial_dev"
  echo "UART_LOG=$log_path"
  echo "REPORT_DIR=$report_dir"
  echo "BUILD_STEP=$do_build"
  echo "FLASH_IMAGE_STEP=$do_flash"
  echo "WRITE_PACK_STEP=$do_write_pack"
  echo "RUN_UART_STEP=$do_run_uart"
  echo "REPORT_STEP=$do_report"
  echo "CAPTURE_TIMEOUT_SEC=$capture_timeout"
} > "$run_manifest"

echo "Run evidence:"
echo "  $run_dir"
echo "Run manifest:"
echo "  $run_manifest"
