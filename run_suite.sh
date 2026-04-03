#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF2'
Usage:
  ./run_suite.sh --tests-dir <dir> [options]
  ./run_suite.sh --test-list <file> [options]

Options:
  --tests-dir <dir>          Directory containing test ELFs (searched with --pattern).
  --test-list <file>         File with one ELF path per line.
  --pattern <glob>           ELF glob inside --tests-dir (default: *.elf).
  --golden-build-root <dir>  Root containing expected ACT .sig files (required).
  --run-root <dir>           Output root for run artifacts (default: ./runs).
  --run-name <name>          Output run name (default: timestamp).
  --serial-dev <path>        UART device for capture (default: /dev/ttyUSB0).
  --baud <rate>              UART baud rate (default: 115200).
  --capture-seconds <n>      UART capture duration per test (default: 40).
  --boot-retries <n>         Retries on boot media errors (default: 2).
  --hard-boot-cmd <cmd>      Command to hard power-cycle board (optional).
  --no-capture               Skip UART capture (you can place uart.log manually).
  -h, --help                 Show this help.

Notes:
  1) This script builds firmware per test and creates uboot_part2_new.bin.
  2) Flash/boot is left to your current method; script pauses for user confirmation.
  3) After capture, it runs extract_act_report.sh and diffs signature.sig vs golden .sig.
EOF2
}

tests_dir=""
test_list=""
pattern="*.elf"
golden_build_root=""
run_root="./runs"
run_name="$(date +%Y%m%d_%H%M%S)"
serial_dev="/dev/ttyUSB0"
baud_rate=115200
capture_seconds=40
boot_retries=2
hard_boot_cmd=""
no_capture=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tests-dir) tests_dir="${2:-}"; shift 2 ;;
    --test-list) test_list="${2:-}"; shift 2 ;;
    --pattern) pattern="${2:-}"; shift 2 ;;
    --golden-build-root) golden_build_root="${2:-}"; shift 2 ;;
    --run-root) run_root="${2:-}"; shift 2 ;;
    --run-name) run_name="${2:-}"; shift 2 ;;
    --serial-dev) serial_dev="${2:-}"; shift 2 ;;
    --baud) baud_rate="${2:-}"; shift 2 ;;
    --capture-seconds) capture_seconds="${2:-}"; shift 2 ;;
    --boot-retries) boot_retries="${2:-}"; shift 2 ;;
    --hard-boot-cmd) hard_boot_cmd="${2:-}"; shift 2 ;;
    --no-capture) no_capture=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$golden_build_root" ]]; then
  echo "ERROR: --golden-build-root is required." >&2
  exit 1
fi
if [[ -z "$tests_dir" && -z "$test_list" ]]; then
  echo "ERROR: provide either --tests-dir or --test-list." >&2
  exit 1
fi
if [[ -n "$tests_dir" && -n "$test_list" ]]; then
  echo "ERROR: use only one of --tests-dir or --test-list." >&2
  exit 1
fi
if [[ ! -x "./extract_act_report.sh" ]]; then
  echo "ERROR: ./extract_act_report.sh not found or not executable." >&2
  exit 1
fi

mapfile -t tests < <(
  if [[ -n "$tests_dir" ]]; then
    find "$tests_dir" -maxdepth 1 -type f -name "$pattern" | sort
  else
    rg -v '^\s*$|^\s*#' "$test_list"
  fi
)

if [[ ${#tests[@]} -eq 0 ]]; then
  echo "ERROR: no tests found." >&2
  exit 1
fi

run_dir="$run_root/$run_name"
mkdir -p "$run_dir"
summary_csv="$run_dir/suite_summary.csv"
failed_txt="$run_dir/failed_tests.txt"
printf "test,result,golden_sig,artifacts_dir\n" > "$summary_csv"
: > "$failed_txt"

echo "Run dir: $run_dir"
echo "Tests: ${#tests[@]}"
if [[ -n "$hard_boot_cmd" ]]; then
  echo "Hard boot hook: enabled"
else
  echo "Hard boot hook: disabled"
fi

run_hard_boot() {
  local reason="$1"
  if [[ -z "$hard_boot_cmd" ]]; then
    return 0
  fi
  echo "[POWER] hard boot: $reason"
  if ! bash -lc "$hard_boot_cmd"; then
    echo "WARN: hard boot command failed for reason: $reason" >&2
    return 1
  fi
  return 0
}

for elf in "${tests[@]}"; do
  test_name="$(basename "$elf" .elf)"
  test_dir="$run_dir/$test_name"
  report_dir="$test_dir/report"
  mkdir -p "$test_dir"

  echo
  echo "=== [$test_name] Build ==="
  make -f Makefile.act clean
  make -f Makefile.act ACT_ELF="$elf"
  mkimage -f vf2_act4.its uboot_part2_new.bin
  truncate -s 4M uboot_part2_new.bin

  cp -f firmware_act.bin "$test_dir/"
  cp -f firmware_act.elf "$test_dir/"
  cp -f uboot_part2_new.bin "$test_dir/"

  max_attempts=$((boot_retries + 1))
  attempt=1
  power_cycled_this_test=0
  final_result=""
  final_golden_sig=""
  while [[ $attempt -le $max_attempts ]]; do
    uart_log="$test_dir/uart_attempt${attempt}.log"
    echo "Prepared image: $test_dir/uboot_part2_new.bin"
    echo "Attempt $attempt/$max_attempts for $test_name"
    echo "Flash using your current method, boot board, then press Enter to continue."
    read -r _

    if [[ "$no_capture" -eq 0 ]]; then
      stty -F "$serial_dev" "$baud_rate" cs8 -cstopb -parenb -ixon -ixoff -icanon -echo raw
      echo "Capturing UART from $serial_dev for ${capture_seconds}s -> $uart_log"
      timeout "${capture_seconds}s" cat "$serial_dev" > "$uart_log" || true
    else
      echo "Capture skipped (--no-capture). Place UART log at: $uart_log"
    fi

    if [[ ! -s "$uart_log" ]]; then
      echo "WARN: missing/empty UART log for $test_name (attempt $attempt)"
      if [[ $attempt -lt $max_attempts ]]; then
        run_hard_boot "$test_name retry after empty UART log" || true
        power_cycled_this_test=1
        attempt=$((attempt + 1))
        continue
      fi
      final_result="NO_UART_LOG"
      break
    fi

    if rg -a -q "dwmci_s: Response Timeout|BOOT fail|Error is 0xffffffff" "$uart_log"; then
      echo "WARN: boot media error detected in UART log (attempt $attempt)"
      if [[ $attempt -lt $max_attempts ]]; then
        run_hard_boot "$test_name retry after boot media error" || true
        power_cycled_this_test=1
        attempt=$((attempt + 1))
        continue
      fi
      final_result="BOOT_FAIL"
      break
    fi

    if rg -a -q "\\[RST\\] trigger watchdog reset|\\[RST\\] watchdog armed" "$uart_log"; then
      echo "INFO: watchdog reset markers seen in UART log"
      run_hard_boot "$test_name watchdog reset" || true
      power_cycled_this_test=1
    fi

    ln -sf "uart_attempt${attempt}.log" "$test_dir/uart.log"
    ./extract_act_report.sh "$uart_log" "$report_dir" >/dev/null

    golden_sig="$(find "$golden_build_root" -type f -name "${test_name}.sig" | head -n1 || true)"
    final_golden_sig="$golden_sig"
    if [[ -z "$golden_sig" ]]; then
      echo "WARN: no golden .sig found for $test_name under $golden_build_root"
      final_result="NO_GOLDEN_SIG"
      break
    fi

    diff_file="$report_dir/signature.diff"
    if diff -u "$golden_sig" "$report_dir/signature.sig" > "$diff_file"; then
      echo "PASS: signature match"
      rm -f "$diff_file"
      final_result="PASS"
    else
      echo "FAIL: signature mismatch -> $diff_file"
      final_result="FAIL"
    fi
    break
  done

  if [[ "$final_result" == "PASS" ]]; then
    printf "%s,%s,%s,%s\n" "$test_name" "$final_result" "$final_golden_sig" "$report_dir" >> "$summary_csv"
  else
    printf "%s,%s,%s,%s\n" "$test_name" "${final_result:-FAIL}" "$final_golden_sig" "$report_dir" >> "$summary_csv"
    echo "$test_name" >> "$failed_txt"
  fi

  if [[ "$power_cycled_this_test" -eq 0 ]]; then
    run_hard_boot "$test_name end of test" || true
  fi
done

echo
echo "Done."
echo "Summary: $summary_csv"
if [[ -s "$failed_txt" ]]; then
  echo "Failures listed in: $failed_txt"
else
  rm -f "$failed_txt"
  echo "All tests passed."
fi
