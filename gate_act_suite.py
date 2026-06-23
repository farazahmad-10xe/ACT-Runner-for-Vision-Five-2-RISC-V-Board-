#!/usr/bin/env python3
"""Gate ACT ELF lists against a runner execution profile.

If an allowlist is provided for lower-privilege profiles, it narrows the run to
matching basenames. Otherwise the full list is allowed through and the runner
itself becomes the execution-environment boundary.
"""

from __future__ import annotations

import argparse
import csv
import fnmatch
from pathlib import Path
from typing import Iterable


def load_tests(tests_dir: str | None, test_list: str | None, pattern: str) -> list[str]:
    if bool(tests_dir) == bool(test_list):
        raise SystemExit("provide exactly one of --tests-dir or --test-list")

    if tests_dir:
        return sorted(str(p) for p in Path(tests_dir).glob(pattern) if p.is_file())

    lines: list[str] = []
    for raw in Path(test_list).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        lines.append(line)
    return lines

def load_patterns(path: str | None) -> list[str]:
    if not path:
        return []
    patterns: list[str] = []
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        patterns.append(line)
    return patterns


def matches_any(name: str, patterns: Iterable[str]) -> bool:
    for pat in patterns:
        if fnmatch.fnmatch(name, pat):
            return True
    return False


def classify(test_path: str, run_priv: str, allow_patterns: list[str]) -> tuple[bool, str]:
    name = Path(test_path).name

    if run_priv == "M":
        return True, "allowed:m_mode_runner"

    if not allow_patterns:
        return True, "allowed:full_lower_privilege_list"

    if matches_any(name, allow_patterns):
        return True, "allowed:allowlist_match"

    return False, "blocked:not_in_allowlist"


def main() -> int:
    ap = argparse.ArgumentParser(description="Gate ACT tests for VF2 runner profiles.")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--tests-dir", help="Directory containing test ELFs")
    src.add_argument("--test-list", help="Text file with one ELF path per line")
    ap.add_argument("--pattern", default="*.elf", help="Glob under --tests-dir (default: *.elf)")
    ap.add_argument("--run-priv", choices=["M", "S", "U"], required=True, help="Runner privilege mode")
    ap.add_argument(
        "--allowlist",
        help="Pattern file for allowed lower-privilege tests; one glob per line against ELF basename",
    )
    ap.add_argument("--out-allowed", required=True, help="Output file for allowed test list")
    ap.add_argument("--out-blocked", required=True, help="Output file for blocked test list")
    ap.add_argument("--out-report", required=True, help="Output CSV classification report")
    args = ap.parse_args()

    tests = load_tests(args.tests_dir, args.test_list, args.pattern)
    allow_patterns = load_patterns(args.allowlist)

    allowed: list[str] = []
    blocked: list[str] = []
    report_rows: list[tuple[str, str, str]] = []

    for test in tests:
        ok, reason = classify(test, args.run_priv, allow_patterns)
        if ok:
            allowed.append(test)
            verdict = "ALLOW"
        else:
            blocked.append(test)
            verdict = "BLOCK"
        report_rows.append((test, verdict, reason))

    Path(args.out_allowed).write_text("\n".join(allowed) + ("\n" if allowed else ""), encoding="utf-8")
    Path(args.out_blocked).write_text("\n".join(blocked) + ("\n" if blocked else ""), encoding="utf-8")

    with Path(args.out_report).open("w", encoding="utf-8", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow(["test", "verdict", "reason"])
        wr.writerows(report_rows)

    print(f"run_priv={args.run_priv} total={len(tests)} allowed={len(allowed)} blocked={len(blocked)}")
    print(f"allowed_list={args.out_allowed}")
    print(f"blocked_list={args.out_blocked}")
    print(f"report={args.out_report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
