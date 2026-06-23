#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <uart.log> [out_dir]" >&2
  exit 1
fi

in_log="$1"
out_dir="${2:-act_report}"

mkdir -p "$out_dir"

clean_log="$out_dir/clean.log"
events_log="$out_dir/events.log"
summary_txt="$out_dir/summary.txt"

tr -d '\000' < "$in_log" | sed 's/\r$//' > "$clean_log"
rg -n "RVCP-SUMMARY|PASS|FAIL|\\[CASE\\]|\\[ACT\\]|\\[TRAP\\]|\\[CTX\\]|\\[REG\\]|\\[CSR\\]|\\[MEMDUMP\\]|\\[RST\\]" "$clean_log" > "$events_log" || true

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
python3 "$script_dir/validate_act_log.py" "$clean_log" "$out_dir" >/dev/null

{
  echo "ACT report generated from firmware verdicts."
  echo "Input log : $in_log"
  echo "Clean log : $clean_log"
  echo "Events log: $events_log"
} > "$summary_txt"

echo "Wrote:"
echo "  $summary_txt"
echo "  $events_log"
echo "  $out_dir/per_case_report.csv"
echo "  $out_dir/suite_summary.txt"
echo "  $out_dir/act_suite_report.txt"
echo "  $out_dir/per_case_act"
