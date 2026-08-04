#!/usr/bin/env python3
"""Create board-neutral pass/fail records from runner UART case reports."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
from pathlib import Path


CASE_START = re.compile(r"\[CASE\] START name=([^\s]+)")
CASE_REPORT = re.compile(
    r"\[CASE\] REPORT name=([^\s]+).*?\bstatus=(PASS|FAIL|TIMEOUT)\b",
    re.DOTALL,
)


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_") or "case"


def read_sail_status(path: Path) -> dict[str, str]:
    statuses: dict[str, str] = {}
    if not path.is_file():
        return statuses
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if len(row) >= 2 and row[0] and row[0] != "test_name":
                statuses[row[0]] = row[1]
    return statuses


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--uart", type=Path, required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--runs-root", type=Path, default=Path("logs/runs"))
    parser.add_argument("--platform", required=True)
    parser.add_argument("--sail-status", type=Path, required=True)
    parser.add_argument("--expected-cases", type=int, default=0)
    args = parser.parse_args()

    text = args.uart.read_text(encoding="utf-8", errors="replace")
    starts = list(CASE_START.finditer(text))
    sail = read_sail_status(args.sail_status)
    run_root = args.runs_root / args.run_id
    run_root.mkdir(parents=True, exist_ok=True)
    if args.uart.resolve() != (run_root / "uart.log").resolve():
        shutil.copy2(args.uart, run_root / "uart.log")

    cases: list[dict] = []
    for index, start in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        case_text = text[start.start() : end]
        name = start.group(1)
        reports = [match for match in CASE_REPORT.finditer(case_text) if match.group(1) == name]
        status = reports[-1].group(2) if reports else "TIMEOUT"
        category = "pass" if status == "PASS" else "board_reported_failure" if status == "FAIL" else "board_timeout"
        root_cause = (
            "The test passed on hardware."
            if status == "PASS"
            else "The board runner reported a self-check failure; platform-specific triage has not yet been applied."
            if status == "FAIL"
            else "The case started but no final runner report was captured."
        )
        case_root = run_root / "per_case" / safe_name(name)
        case_root.mkdir(parents=True, exist_ok=True)
        (case_root / "uart.log").write_text(case_text, encoding="utf-8")
        evidence = {
            "test_name": name,
            "platform": args.platform,
            "status": status,
            "sail_status": sail.get(name, "NOT RUN"),
            "category": category,
            "root_cause": root_cause,
        }
        (case_root / "evidence.json").write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (case_root / "report.md").write_text(
            f"# {name}\n\n- Platform: `{args.platform}`\n- Sail: `{evidence['sail_status']}`\n"
            f"- Hardware: `{status}`\n- Category: `{category}`\n\n{root_cause}\n",
            encoding="utf-8",
        )
        cases.append(
            {
                **evidence,
                "report": str(case_root / "report.md"),
                "verification_state": "closed_pass" if status == "PASS" else "needs_platform_triage",
                "verification_confidence": "high" if status == "PASS" else "low",
            }
        )

    (run_root / "cases.json").write_text(
        json.dumps(cases, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (run_root / "collection.json").write_text(
        json.dumps([{"test_name": case["test_name"], "missing": False} for case in cases], indent=2) + "\n",
        encoding="utf-8",
    )
    with (run_root / "cases.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("test_name", "sail_status", "status", "category"))
        writer.writeheader()
        writer.writerows({key: case[key] for key in writer.fieldnames} for case in cases)
    passed = sum(case["status"] == "PASS" for case in cases)
    failed = sum(case["status"] != "PASS" for case in cases)
    (run_root / "summary.md").write_text(
        f"# {args.run_id}\n\n- Platform: `{args.platform}`\n- Cases: {len(cases)}\n"
        f"- PASS: {passed}\n- FAIL/TIMEOUT: {failed}\n",
        encoding="utf-8",
    )
    if args.expected_cases and len(cases) != args.expected_cases:
        print(f"Expected {args.expected_cases} cases but captured {len(cases)}", flush=True)
        return 2
    print(f"Collected {len(cases)} cases for {args.platform}: {passed} PASS, {failed} FAIL/TIMEOUT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
