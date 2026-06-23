#!/usr/bin/env python3
"""Promote validated ACT cases into a lower-privilege allowlist."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def read_existing_allowlist(path: Path) -> tuple[list[str], set[str]]:
    lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
    entries = {line.strip() for line in lines if line.strip() and not line.lstrip().startswith("#")}
    return lines, entries


def collect_passed_cases(per_case_csv: Path) -> list[str]:
    passed: list[str] = []
    with per_case_csv.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            verdict = (row.get("final_verdict") or "").strip().upper()
            name = (row.get("name") or "").strip()
            if verdict != "PASS" or not name:
                continue
            if not name.endswith(".elf"):
                name = f"{name}.elf"
            passed.append(name)
    return passed


def main() -> int:
    ap = argparse.ArgumentParser(description="Promote validated ACT cases into a runner allowlist.")
    ap.add_argument("--per-case-csv", required=True, help="per_case_report.csv from extract_act_report.sh")
    ap.add_argument("--allowlist", required=True, help="Allowlist file to update")
    ap.add_argument(
        "--mode",
        choices=["exact"],
        default="exact",
        help="Promotion mode; currently only exact basename entries are emitted",
    )
    args = ap.parse_args()

    allowlist_path = Path(args.allowlist)
    original_lines, existing = read_existing_allowlist(allowlist_path)
    passed = collect_passed_cases(Path(args.per_case_csv))

    new_entries: list[str] = []
    for item in passed:
        if item in existing:
            continue
        existing.add(item)
        new_entries.append(item)

    out_lines = list(original_lines)
    if new_entries:
        if out_lines and out_lines[-1].strip():
            out_lines.append("")
        out_lines.append("# Promoted from validated runner results")
        out_lines.extend(sorted(new_entries))

    allowlist_path.write_text("\n".join(out_lines) + ("\n" if out_lines else ""), encoding="utf-8")

    print(f"per_case_csv={args.per_case_csv}")
    print(f"allowlist={args.allowlist}")
    print(f"promoted={len(new_entries)}")
    for item in sorted(new_entries):
        print(item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
