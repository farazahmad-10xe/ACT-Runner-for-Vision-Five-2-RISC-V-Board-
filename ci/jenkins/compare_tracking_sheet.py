#!/usr/bin/env python3
"""Compare the public tracking-sheet snapshot with one Jenkins execution."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sheet-csv", type=Path, required=True)
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--run-root", type=Path, required=True)
    parser.add_argument("--issue-catalog", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def read_status_tsv(path: Path) -> dict[str, str]:
    statuses: dict[str, str] = {}
    if not path.is_file():
        return statuses
    with path.open(newline="", encoding="utf-8", errors="replace") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if len(row) >= 2 and row[0] and row[0].lower() != "test_name":
                statuses[row[0]] = row[1]
    return statuses


def read_cases(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError, TypeError):
        return {}
    statuses: dict[str, str] = {}
    for item in payload if isinstance(payload, list) else []:
        if not isinstance(item, dict):
            continue
        name = str(item.get("test_name", ""))
        if name:
            statuses[name] = str(item.get("status") or "NOT RUN")
    return statuses


def result_word(value: str) -> str:
    upper = value.strip().upper()
    if not upper:
        return "NOT RECORDED"
    if "TIMEOUT" in upper or "TIME OUT" in upper:
        return "TIMEOUT"
    if re.search(r"\bFAIL(?:ED|URE)?\b", upper):
        return "FAIL"
    if re.search(r"\bPASS(?:ED)?\b", upper):
        return "PASS"
    if "NOT RUN" in upper:
        return "NOT RUN"
    return "OTHER"


def comparison(sheet_value: str, jenkins_value: str) -> str:
    sheet_result = result_word(sheet_value)
    jenkins_result = result_word(jenkins_value)
    if sheet_result in {"NOT RECORDED", "OTHER"}:
        return "NO DIRECT SHEET VERDICT"
    return "MATCH" if sheet_result == jenkins_result else "DIFFERENT"


def main() -> int:
    args = parse_args()
    sail = read_status_tsv(args.state_root / "sail_reference_status.tsv")
    spike = read_status_tsv(args.state_root / "spike_status.tsv")
    vf2 = read_cases(args.run_root / "cases.json")
    issue_catalog = json.loads(args.issue_catalog.read_text(encoding="utf-8"))
    tickets = issue_catalog.get("tickets", {})
    test_tickets = issue_catalog.get("tests", {})

    with args.sheet_csv.open(newline="", encoding="utf-8-sig", errors="replace") as stream:
        rows = list(csv.DictReader(stream))

    fields = (
        "test_name",
        "sheet_spike",
        "sheet_vf2",
        "sheet_workflow_status",
        "assignee",
        "jenkins_sail",
        "jenkins_spike",
        "jenkins_vf2",
        "spike_comparison",
        "vf2_comparison",
        "github_tickets",
        "failure_cause",
        "comments",
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    written = 0
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            name = str(row.get("name", "")).strip()
            if not name:
                continue
            sheet_spike = str(row.get("Spike Pass/Fail", "") or "")
            sheet_vf2 = str(row.get("Vission2 Pass/Fail", "") or "")
            issue_urls = [
                str(tickets[key].get("url", ""))
                for key in test_tickets.get(name, [])
                if key in tickets and tickets[key].get("url")
            ]
            writer.writerow(
                {
                    "test_name": name,
                    "sheet_spike": sheet_spike,
                    "sheet_vf2": sheet_vf2,
                    "sheet_workflow_status": str(row.get("Status", "") or ""),
                    "assignee": str(row.get("Assignee", "") or ""),
                    "jenkins_sail": sail.get(name, "NOT RUN"),
                    "jenkins_spike": spike.get(name, "NOT RUN"),
                    "jenkins_vf2": vf2.get(name, "NOT RUN"),
                    "spike_comparison": comparison(sheet_spike, spike.get(name, "NOT RUN")),
                    "vf2_comparison": comparison(sheet_vf2, vf2.get(name, "NOT RUN")),
                    "github_tickets": " ".join(issue_urls),
                    "failure_cause": str(row.get("Failure cause", "") or ""),
                    "comments": str(row.get("Comments", "") or ""),
                }
            )
            written += 1
    print(f"Tracking comparison: {args.output} ({written} sheet tests)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
