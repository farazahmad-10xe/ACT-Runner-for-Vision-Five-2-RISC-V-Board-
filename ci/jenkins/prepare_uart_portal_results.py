#!/usr/bin/env python3
"""Convert UART-stream batch output to the shared Jenkins portal contract."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
from pathlib import Path


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-root", type=Path, required=True)
    args = parser.parse_args()

    run_root = args.run_root.resolve()
    summary_path = run_root / "summary.json"
    payload = json.loads(summary_path.read_text(encoding="utf-8"))
    results = payload.get("results", [])
    if not isinstance(results, list):
        raise ValueError(f"{summary_path}: results must be a list")

    cases: list[dict[str, object]] = []
    for result in results:
        if not isinstance(result, dict) or not result.get("name"):
            continue
        name = str(result["name"])
        status = str(result.get("status", "ERROR")).upper()
        source_log = Path(str(result.get("uart_log", "")))
        relative_log = Path("per_case") / safe_name(name) / "uart.log"
        destination_log = run_root / relative_log
        if source_log.is_file():
            destination_log.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_log, destination_log)
        cases.append(
            {
                "test_name": name,
                "status": status,
                "tohost": str(result.get("tohost", "")),
                "elapsed_seconds": result.get("elapsed_seconds", 0),
                "sender_returncode": result.get("sender_returncode"),
                "root_cause": "" if status == "PASS" else str(result.get("error", status)),
                "uart_log": str(relative_log) if destination_log.is_file() else "",
                "transport": "uart_stream",
            }
        )

    (run_root / "cases.json").write_text(
        json.dumps(cases, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    with (run_root / "cases.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=(
                "test_name",
                "status",
                "tohost",
                "elapsed_seconds",
                "sender_returncode",
                "root_cause",
                "uart_log",
                "transport",
            ),
        )
        writer.writeheader()
        writer.writerows(cases)

    counts = payload.get("counts", {})
    lines = [
        "# VF2 UART-stream results",
        "",
        f"- Planned: {payload.get('planned', len(cases))}",
        f"- Completed: {payload.get('completed', len(cases))}",
        f"- PASS: {counts.get('PASS', 0)}",
        f"- FAIL: {counts.get('FAIL', 0)}",
        f"- TIMEOUT: {counts.get('TIMEOUT', 0)}",
        f"- ERROR: {counts.get('ERROR', 0)}",
        "",
        "| Test | Status | Time (s) | tohost |",
        "|---|---:|---:|---|",
    ]
    lines.extend(
        f"| {case['test_name']} | {case['status']} | {case['elapsed_seconds']} | {case['tohost']} |"
        for case in cases
    )
    (run_root / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Prepared {len(cases)} UART cases for portal publication in {run_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
