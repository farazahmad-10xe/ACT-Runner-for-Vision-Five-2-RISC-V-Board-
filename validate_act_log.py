#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional


CASE_START_RE = re.compile(r"^\[CASE\] START name=(.+)$")
CASE_REPORT_RE = re.compile(r"^\[CASE\] REPORT name=(.+)$")
SIGQ_RE = re.compile(r"^\[SIGQ\] (0x[0-9a-fA-F]+) : (0x[0-9a-fA-F]+)$")
SIGB_RE = re.compile(r"^\[SIGB\] (0x[0-9a-fA-F]+) : 0x([0-9a-fA-F]{2})$")
RVCP_RE = re.compile(r'^RVCP-RESULT: Test File "(.+)\.S": (PASSED|FAILED)$')


def clean_name(name: str) -> str:
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in name)


def parse_kv_tokens(line: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        out[key] = value
    return out


@dataclass
class CaseRecord:
    index: int
    name: str
    status: str = "ERROR"
    rc: str = "0x0000000000000000"
    tohost_addr: str = "0x0000000000000000"
    tohost_value: str = "0x0000000000000000"
    sig_begin: str = "0x0000000000000000"
    sig_end: str = "0x0000000000000000"
    sig_bytes: str = "0x0000000000000000"
    exit_pc: str = "0x0000000000000000"
    trap_mcause: str = ""
    trap_mepc: str = ""
    exit_mtval: str = ""
    exit_mstatus: str = ""
    exit_symbol: str = ""
    exit_symbol_addr: str = ""
    rvcp_result: str = ""
    events: List[str] = field(default_factory=list)
    sig_map: Dict[int, int] = field(default_factory=dict)
    regs: Dict[str, str] = field(default_factory=dict)

    def add_sigq(self, addr: int, value: int) -> None:
        for off in range(8):
            self.sig_map[addr + off] = (value >> (off * 8)) & 0xFF

    def add_sigb(self, addr: int, value: int) -> None:
        self.sig_map[addr] = value

    def signature_range(self) -> Optional[range]:
        if not self.sig_map:
            return None
        lo = min(self.sig_map)
        hi = max(self.sig_map)
        return range(lo, hi + 1)

    def signature_bytes_blob(self) -> bytes:
        sig_range = self.signature_range()
        if sig_range is None:
            return b""
        return bytes(self.sig_map.get(addr, 0) for addr in sig_range)

    def signature_sig_lines(self) -> List[str]:
        sig_range = self.signature_range()
        if sig_range is None:
            return []
        lo = sig_range.start
        hi = sig_range.stop - 1
        lines: List[str] = []
        addr = lo
        while addr <= hi:
            chunk = bytes(self.sig_map.get(addr + off, 0) for off in range(8))
            lines.append(chunk[::-1].hex())
            addr += 8
        return lines


def parse_log(log_path: Path) -> List[CaseRecord]:
    lines = log_path.read_text(encoding="utf-8", errors="replace").replace("\x00", "").splitlines()
    cases: List[CaseRecord] = []
    current: Optional[CaseRecord] = None

    for raw_line in lines:
        line = raw_line.rstrip("\r")

        m = CASE_START_RE.match(line)
        if m:
            current = CaseRecord(index=len(cases) + 1, name=m.group(1))
            current.events.append(line)
            cases.append(current)
            continue

        if current is None:
            continue

        current.events.append(line)

        m = CASE_REPORT_RE.match(line)
        if m:
            kv = parse_kv_tokens(line)
            current.name = kv.get("name", current.name)
            current.status = kv.get("status", current.status)
            current.rc = kv.get("rc", current.rc)
            current.tohost_addr = kv.get("tohost_addr", current.tohost_addr)
            current.tohost_value = kv.get("tohost_value", current.tohost_value)
            current.sig_begin = kv.get("sig_begin", current.sig_begin)
            current.sig_end = kv.get("sig_end", current.sig_end)
            current.sig_bytes = kv.get("sig_bytes", current.sig_bytes)
            current.exit_pc = kv.get("exit_pc", current.exit_pc)
            current.trap_mcause = kv.get("trap_mcause", current.trap_mcause)
            current.trap_mepc = kv.get("trap_mepc", current.trap_mepc)
            continue

        if line.startswith("[CTX] "):
            kv = parse_kv_tokens(line)
            current.trap_mcause = kv.get("exit_mcause", current.trap_mcause)
            current.trap_mepc = kv.get("exit_mepc", current.trap_mepc)
            current.exit_mtval = kv.get("exit_mtval", current.exit_mtval)
            current.exit_mstatus = kv.get("exit_mstatus", current.exit_mstatus)
            current.exit_symbol = kv.get("exit_symbol", current.exit_symbol)
            current.exit_symbol_addr = kv.get("exit_symbol_addr", current.exit_symbol_addr)
            continue

        if line.startswith("[REG] "):
            current.regs.update(parse_kv_tokens(line))
            continue

        m = RVCP_RE.match(line)
        if m:
            current.rvcp_result = m.group(2)
            continue

        m = SIGQ_RE.match(line)
        if m:
            current.add_sigq(int(m.group(1), 16), int(m.group(2), 16))
            continue

        m = SIGB_RE.match(line)
        if m:
            current.add_sigb(int(m.group(1), 16), int(m.group(2), 16))
            continue

    return cases


def find_golden_sig(golden_root: Path, case_name: str) -> Optional[Path]:
    matches = sorted(golden_root.rglob(f"{case_name}.sig"))
    return matches[0] if matches else None


def compare_sig_lines(expected: Path, actual_lines: List[str]) -> str:
    expected_lines = [line.strip().lower() for line in expected.read_text(encoding="utf-8", errors="replace").splitlines() if line.strip()]
    return "MATCH" if expected_lines == [line.lower() for line in actual_lines] else "MISMATCH"


def write_case_artifacts(case_root: Path, case: CaseRecord) -> List[str]:
    case_root.mkdir(parents=True, exist_ok=True)

    (case_root / "events.log").write_text("\n".join(case.events) + ("\n" if case.events else ""), encoding="utf-8")

    report_lines = [
        f'RVCP-REPORT: Test File "{case.name}.S"',
        f"STATUS      : {case.status}",
        f"RC          : {case.rc}",
        f"TOHOST_ADDR : {case.tohost_addr}",
        f"TOHOST_VAL  : {case.tohost_value}",
        f"SIG_BEGIN   : {case.sig_begin}",
        f"SIG_END     : {case.sig_end}",
        f"SIG_BYTES   : {case.sig_bytes}",
        f"EXIT_PC     : {case.exit_pc}",
    ]
    if case.trap_mcause:
        report_lines.append(f"TRAP_MCAUSE : {case.trap_mcause}")
    if case.trap_mepc:
        report_lines.append(f"TRAP_MEPC   : {case.trap_mepc}")
    if case.exit_mtval:
        report_lines.append(f"EXIT_MTVAL  : {case.exit_mtval}")
    if case.exit_mstatus:
        report_lines.append(f"EXIT_MSTATUS: {case.exit_mstatus}")
    if case.exit_symbol:
        report_lines.append(f"EXIT_SYMBOL : {case.exit_symbol}")
    if case.exit_symbol_addr:
        report_lines.append(f"EXIT_SYMADR : {case.exit_symbol_addr}")
    if case.regs:
        for reg_name in (
            "ra", "sp", "gp", "tp", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
            "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9",
            "s10", "s11", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
            "mepc",
        ):
            if reg_name in case.regs:
                report_lines.append(f"REG_{reg_name.upper():<8}: {case.regs[reg_name]}")
    report_lines.append(f'RVCP-SUMMARY: Test File "{case.name}.S": {"PASSED" if case.status == "PASS" else "FAILED"}')
    (case_root / "case_report.txt").write_text("\n".join(report_lines) + "\n", encoding="utf-8")

    addr_lines: List[str] = []
    for addr in sorted(case.sig_map):
        addr_lines.append(f"0x{addr:016x} 0x{case.sig_map[addr]:02x}")
    (case_root / "signature_addr_val.log").write_text("\n".join(addr_lines) + ("\n" if addr_lines else ""), encoding="utf-8")

    sig_lines = case.signature_sig_lines()
    (case_root / "signature.sig").write_text("\n".join(sig_lines) + ("\n" if sig_lines else ""), encoding="utf-8")
    (case_root / "signature.bin").write_bytes(case.signature_bytes_blob())
    return sig_lines


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate ACT UART log against golden signatures.")
    ap.add_argument("uart_log", help="UART log to parse")
    ap.add_argument("out_dir", nargs="?", default="act_report", help="Output directory")
    ap.add_argument("--golden-root", help="Root directory containing expected .sig files")
    args = ap.parse_args()

    uart_log = Path(args.uart_log)
    out_dir = Path(args.out_dir)
    golden_root = Path(args.golden_root).expanduser() if args.golden_root else None

    out_dir.mkdir(parents=True, exist_ok=True)
    per_case_dir = out_dir / "per_case_act"
    per_case_dir.mkdir(parents=True, exist_ok=True)

    cases = parse_log(uart_log)

    per_case_csv = out_dir / "per_case_report.csv"
    golden_csv = out_dir / "golden_compare.csv"
    suite_summary = out_dir / "suite_summary.txt"
    act_suite_report = out_dir / "act_suite_report.txt"

    with per_case_csv.open("w", newline="", encoding="utf-8") as fh_csv, golden_csv.open("w", newline="", encoding="utf-8") as fh_golden:
        csv_writer = csv.writer(fh_csv)
        csv_writer.writerow([
            "name",
            "status",
            "rc",
            "tohost_addr",
            "tohost_value",
            "sig_begin",
            "sig_end",
            "sig_bytes",
            "golden_status",
            "final_verdict",
        ])

        golden_writer = csv.writer(fh_golden)
        golden_writer.writerow(["case", "status", "golden_sig", "local_sig"])

        golden_match_cases = 0
        golden_mismatch_cases = 0
        golden_no_golden_cases = 0
        final_pass_cases = 0
        final_fail_cases = 0

        for case in cases:
            case_root = per_case_dir / f"{case.index:03d}_{clean_name(case.name)}"
            sig_lines = write_case_artifacts(case_root, case)

            golden_status = "NO_COMPARE"
            golden_sig_path = ""
            if golden_root and golden_root.is_dir():
                golden = find_golden_sig(golden_root, case.name)
                if golden is None:
                    golden_status = "NO_GOLDEN"
                    golden_no_golden_cases += 1
                else:
                    golden_sig_path = str(golden)
                    golden_status = compare_sig_lines(golden, sig_lines)
                    if golden_status == "MATCH":
                        golden_match_cases += 1
                    else:
                        golden_mismatch_cases += 1
                        diff_lines = []
                        expected_lines = [line.strip().lower() for line in golden.read_text(encoding="utf-8", errors="replace").splitlines() if line.strip()]
                        for idx, (exp, act) in enumerate(zip(expected_lines, sig_lines), start=1):
                            if exp != act:
                                diff_lines.append(f"line {idx}: expected {exp} actual {act}")
                        if len(expected_lines) != len(sig_lines):
                            diff_lines.append(f"line_count: expected {len(expected_lines)} actual {len(sig_lines)}")
                        (case_root / "signature.diff").write_text("\n".join(diff_lines) + ("\n" if diff_lines else ""), encoding="utf-8")

            local_sig_path = str(case_root / "signature.sig")
            golden_writer.writerow([case.name, golden_status, golden_sig_path, local_sig_path])

            final_verdict = "PASS" if case.status == "PASS" and golden_status == "MATCH" else "FAIL"
            if final_verdict == "PASS":
                final_pass_cases += 1
            else:
                final_fail_cases += 1

            csv_writer.writerow([
                case.name,
                case.status,
                case.rc,
                case.tohost_addr,
                case.tohost_value,
                case.sig_begin,
                case.sig_end,
                case.sig_bytes,
                golden_status,
                final_verdict,
            ])

    suite_summary.write_text(
        "\n".join(
            [
                f"total_cases={len(cases)}",
                f"golden_match_cases={golden_match_cases}",
                f"golden_mismatch_cases={golden_mismatch_cases}",
                f"golden_no_golden_cases={golden_no_golden_cases}",
                f"final_pass_cases={final_pass_cases}",
                f"final_fail_cases={final_fail_cases}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    with act_suite_report.open("w", encoding="utf-8") as fh:
        fh.write("RVCP-SUITE-REPORT: ACT RUN\n")
        fh.write(f"INPUT_LOG   : {uart_log}\n")
        fh.write(f"TOTAL_CASES : {len(cases)}\n")
        fh.write(f"FINAL_PASS_CASES : {final_pass_cases}\n")
        fh.write(f"FINAL_FAIL_CASES : {final_fail_cases}\n")
        fh.write(f"GOLDEN_COMPARE : {golden_csv if golden_root else 'disabled'}\n\n")
        fh.write("PER-CASE:\n")
        for case in cases:
            fh.write(f'RVCP-SUMMARY: Test File "{case.name}.S": {"PASSED" if case.status == "PASS" else "FAILED"}\n')

    print(f"Wrote: {per_case_csv}")
    print(f"Wrote: {golden_csv}")
    print(f"Wrote: {suite_summary}")
    print(f"Wrote: {act_suite_report}")
    print(f"Wrote: {per_case_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
