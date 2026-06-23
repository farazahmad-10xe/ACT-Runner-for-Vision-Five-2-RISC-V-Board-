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
RIESCUE_FAIL_CONTEXT_RE = re.compile(r"^\[RIESCUE\] fail_context\b")
RVCP_RE = re.compile(r'^RVCP-RESULT: Test File "(.+)\.S": (PASSED|FAILED)$')
RVCP_SUMMARY_RE = re.compile(r'^RVCP-SUMMARY: TEST (PASSED|FAILED) - Test File "(.+)\.S"$')
ZERO_HEX = "0x0000000000000000"
NEGATIVE_FAIL_MCAUSE = "0x00000000000000ff"
INVALID_PC = "0xffffffffffffffff"


def clean_name(name: str) -> str:
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in name)


def infer_case_mode(name: str) -> str:
    if name.endswith("_machine"):
        return "machine"
    if name.endswith("_super"):
        return "supervisor"
    if name.endswith("_user"):
        return "user"
    return "unknown"


CAUSE_NAMES = {
    0: "misaligned instruction address",
    1: "instruction access fault",
    2: "illegal instruction",
    3: "breakpoint",
    4: "misaligned load",
    5: "load access fault",
    6: "misaligned store/amo",
    7: "store/amo access fault",
    8: "ecall from U-mode",
    9: "ecall from S-mode",
    10: "reserved",
    11: "ecall from M-mode",
    12: "instruction page fault",
    13: "load page fault",
    14: "reserved",
    15: "store/amo page fault",
    16: "double trap",
    18: "software check",
    19: "hardware error",
}

INTERRUPT_CAUSE_NAMES = {
    1: "supervisor software interrupt",
    3: "machine software interrupt",
    5: "supervisor timer interrupt",
    7: "machine timer interrupt",
    9: "supervisor external interrupt",
    11: "machine external interrupt",
    13: "counter overflow interrupt",
}


def decode_mcause(value: str) -> str:
    if not value:
        return ""
    try:
        raw = int(value, 16)
    except ValueError:
        return ""

    is_interrupt = bool(raw >> 63)
    cause_code = raw & ((1 << 63) - 1)
    names = INTERRUPT_CAUSE_NAMES if is_interrupt else CAUSE_NAMES
    desc = names.get(cause_code, "unknown")
    if is_interrupt:
        return f"interrupt: {desc}"
    return desc


def is_zero_hex(value: str) -> bool:
    if not value:
        return True
    try:
        return int(value, 16) == 0
    except ValueError:
        return False


def parse_kv_tokens(line: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        out[key] = value
    return out


def update_case_from_report_kv(case: "CaseRecord", kv: Dict[str, str]) -> None:
    case.name = kv.get("name", case.name)
    case.status = kv.get("status", case.status)
    case.rc = kv.get("rc", case.rc)
    case.tohost_addr = kv.get("tohost_addr", case.tohost_addr)
    case.tohost_value = kv.get("tohost_value", case.tohost_value)
    case.exit_pc = kv.get("exit_pc", case.exit_pc)
    case.trap_mcause = kv.get("trap_mcause", case.trap_mcause)
    case.trap_mcause_text = decode_mcause(case.trap_mcause)
    case.trap_mepc = kv.get("trap_mepc", case.trap_mepc)


@dataclass
class CaseRecord:
    index: int
    name: str
    attempt: int = 1
    status: str = "ERROR"
    rc: str = "0x0000000000000000"
    tohost_addr: str = "0x0000000000000000"
    tohost_value: str = "0x0000000000000000"
    exit_pc: str = "0x0000000000000000"
    trap_mcause: str = ""
    trap_mcause_text: str = ""
    trap_mepc: str = ""
    exit_mtval: str = ""
    exit_mstatus: str = ""
    exit_symbol: str = ""
    exit_symbol_addr: str = ""
    ctx_reason: str = ""
    handled_trap: bool = False
    handled_trap_mode: str = ""
    handled_mcause: str = ""
    handled_mcause_text: str = ""
    handled_mepc: str = ""
    handled_insn: str = ""
    riescue_fail_mepc: str = ""
    riescue_expected_mcause: str = ""
    riescue_actual_mcause: str = ""
    riescue_actual_mcause_text: str = ""
    rvcp_result: str = ""
    events: List[str] = field(default_factory=list)
    regs: Dict[str, str] = field(default_factory=dict)

    def failure_exit_pc(self) -> str:
        if self.handled_trap and self.status == "PASS":
            return ZERO_HEX
        return self.exit_pc

    def failure_trap_mcause(self) -> str:
        if self.handled_trap and self.status == "PASS":
            return ""
        if self.status == "PASS" and is_zero_hex(self.trap_mcause):
            return ""
        return self.trap_mcause

    def failure_trap_mcause_text(self) -> str:
        if self.handled_trap and self.status == "PASS":
            return ""
        if self.status == "PASS" and is_zero_hex(self.trap_mcause):
            return ""
        return self.trap_mcause_text

    def is_expected_fail(self) -> bool:
        return (
            self.status == "FAIL"
            and self.riescue_actual_mcause.lower() == NEGATIVE_FAIL_MCAUSE
            and self.riescue_fail_mepc.lower() == INVALID_PC
        )

    def final_verdict(self) -> str:
        if self.status == "PASS":
            return "PASS"
        if self.is_expected_fail():
            return "EXPECTED_FAIL"
        return "FAIL"


def parse_log(log_path: Path) -> List[CaseRecord]:
    lines = log_path.read_text(encoding="utf-8", errors="replace").replace("\x00", "").splitlines()
    cases: List[CaseRecord] = []
    current: Optional[CaseRecord] = None
    attempts_by_name: Dict[str, int] = {}

    for raw_line in lines:
        line = raw_line.rstrip("\r")

        m = CASE_START_RE.match(line)
        if m:
            name = m.group(1)
            attempt = attempts_by_name.get(name, 0) + 1
            attempts_by_name[name] = attempt
            current = CaseRecord(index=len(cases) + 1, name=name, attempt=attempt)
            current.events.append(line)
            cases.append(current)
            continue

        if current is None:
            continue

        current.events.append(line)

        m = CASE_REPORT_RE.match(line)
        if m:
            update_case_from_report_kv(current, parse_kv_tokens(line))
            continue

        kv = parse_kv_tokens(line)
        if any(
            key in kv
            for key in (
                "status",
                "rc",
                "tohost_addr",
                "tohost_value",
                "exit_pc",
                "trap_mcause",
                "trap_mepc",
            )
        ):
            update_case_from_report_kv(current, kv)

        if line.startswith("[CTX] "):
            kv = parse_kv_tokens(line)
            current.ctx_reason = kv.get("reason", current.ctx_reason)
            current.trap_mcause = kv.get("exit_mcause", current.trap_mcause)
            current.trap_mcause_text = decode_mcause(current.trap_mcause)
            current.trap_mepc = kv.get("exit_mepc", current.trap_mepc)
            current.exit_mtval = kv.get("exit_mtval", current.exit_mtval)
            current.exit_mstatus = kv.get("exit_mstatus", current.exit_mstatus)
            current.exit_symbol = kv.get("exit_symbol", current.exit_symbol)
            current.exit_symbol_addr = kv.get("exit_symbol_addr", current.exit_symbol_addr)
            continue

        if line.startswith("[HANDLED] "):
            kv = parse_kv_tokens(line)
            current.handled_trap = kv.get("trap_was_handled", "0") == "1"
            if kv.get("final_status") == "PASS":
                current.status = "PASS"
                current.tohost_value = "0x0000000000000001"
            current.handled_trap_mode = kv.get("handled_trap_mode", current.handled_trap_mode)
            current.handled_mcause = kv.get("handled_mcause", current.handled_mcause)
            current.handled_mcause_text = decode_mcause(current.handled_mcause)
            current.handled_mepc = kv.get("handled_mepc", current.handled_mepc)
            current.handled_insn = kv.get("handled_insn", current.handled_insn)
            continue

        if RIESCUE_FAIL_CONTEXT_RE.match(line):
            kv = parse_kv_tokens(line)
            current.riescue_fail_mepc = kv.get("mepc", current.riescue_fail_mepc)
            current.riescue_expected_mcause = kv.get("expected_mcause", current.riescue_expected_mcause)
            current.riescue_actual_mcause = kv.get("actual_mcause", current.riescue_actual_mcause)
            current.riescue_actual_mcause_text = decode_mcause(current.riescue_actual_mcause)
            continue

        if line.startswith("[REG] "):
            current.regs.update(parse_kv_tokens(line))
            continue

        m = RVCP_RE.match(line)
        if m:
            current.rvcp_result = m.group(2)
            continue

        m = RVCP_SUMMARY_RE.match(line)
        if m:
            current.rvcp_result = m.group(1)
            continue

    return cases


def write_case_artifacts(case_root: Path, case: CaseRecord) -> None:
    case_root.mkdir(parents=True, exist_ok=True)
    (case_root / "events.log").write_text("\n".join(case.events) + ("\n" if case.events else ""), encoding="utf-8")

    report_lines = [
        f'RVCP-REPORT: Test File "{case.name}.S"',
        f"ATTEMPT     : {case.attempt}",
        f"STATUS      : {case.status}",
        f"RC          : {case.rc}",
        f"TOHOST_ADDR : {case.tohost_addr}",
        f"TOHOST_VAL  : {case.tohost_value}",
        f"EXIT_PC     : {case.failure_exit_pc()}",
    ]
    if case.failure_trap_mcause():
        if case.failure_trap_mcause_text():
            report_lines.append(f"TRAP_MCAUSE : {case.failure_trap_mcause()} ({case.failure_trap_mcause_text()})")
        else:
            report_lines.append(f"TRAP_MCAUSE : {case.failure_trap_mcause()}")
    if case.handled_trap:
        report_lines.append("HANDLED_TRAP: yes")
        if case.handled_trap_mode:
            report_lines.append(f"HANDLED_MODE: {case.handled_trap_mode}")
        if case.handled_mcause:
            if case.handled_mcause_text:
                report_lines.append(f"HANDLED_CAUS: {case.handled_mcause} ({case.handled_mcause_text})")
            else:
                report_lines.append(f"HANDLED_CAUS: {case.handled_mcause}")
        if case.handled_mepc:
            report_lines.append(f"HANDLED_EPC : {case.handled_mepc}")
        if case.handled_insn:
            report_lines.append(f"HANDLED_INSN: {case.handled_insn}")
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


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate ACT UART log against firmware-reported verdicts.")
    ap.add_argument("uart_log", help="UART log to parse")
    ap.add_argument("out_dir", nargs="?", default="act_report", help="Output directory")
    args = ap.parse_args()

    uart_log = Path(args.uart_log)
    out_dir = Path(args.out_dir)

    out_dir.mkdir(parents=True, exist_ok=True)
    per_case_dir = out_dir / "per_case_act"
    per_case_dir.mkdir(parents=True, exist_ok=True)

    cases = parse_log(uart_log)
    latest_by_name: Dict[str, CaseRecord] = {}
    for case in cases:
        latest_by_name[case.name] = case
    deduped_cases = list(latest_by_name.values())

    per_case_csv = out_dir / "per_case_report.csv"
    suite_summary = out_dir / "suite_summary.txt"
    act_suite_report = out_dir / "act_suite_report.txt"

    final_pass_cases = 0
    final_fail_cases = 0

    with per_case_csv.open("w", newline="", encoding="utf-8") as fh_csv:
        csv_writer = csv.writer(fh_csv)
        csv_writer.writerow([
            "name",
            "mode",
            "attempts",
            "status",
            "rc",
            "tohost_addr",
            "tohost_value",
            "exit_pc",
            "trap_mcause",
            "trap_mcause_text",
            "handled_trap",
            "handled_mepc",
            "handled_mcause",
            "handled_mcause_text",
            "riescue_fail_mepc",
            "riescue_expected_mcause",
            "riescue_actual_mcause",
            "riescue_actual_mcause_text",
            "final_verdict",
        ])

        for case in deduped_cases:
            case_root = per_case_dir / f"{case.index:03d}_{clean_name(case.name)}"
            write_case_artifacts(case_root, case)

            final_verdict = case.final_verdict()
            if final_verdict == "PASS":
                final_pass_cases += 1
            elif final_verdict == "EXPECTED_FAIL":
                pass
            else:
                final_fail_cases += 1

            csv_writer.writerow([
                case.name,
                infer_case_mode(case.name),
                case.attempt,
                case.status,
                case.rc,
                case.tohost_addr,
                case.tohost_value,
                case.failure_exit_pc(),
                case.failure_trap_mcause(),
                case.failure_trap_mcause_text(),
                "yes" if case.handled_trap else "no",
                case.handled_mepc,
                case.handled_mcause,
                case.handled_mcause_text,
                case.riescue_fail_mepc,
                case.riescue_expected_mcause,
                case.riescue_actual_mcause,
                case.riescue_actual_mcause_text,
                final_verdict,
            ])

    suite_summary.write_text(
        "\n".join(
            [
                f"total_case_executions={len(cases)}",
                f"unique_cases={len(deduped_cases)}",
                f"final_pass_cases={final_pass_cases}",
                f"final_fail_cases={final_fail_cases}",
                f"expected_fail_cases={sum(1 for case in deduped_cases if case.final_verdict() == 'EXPECTED_FAIL')}",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    with act_suite_report.open("w", encoding="utf-8") as fh:
        fh.write("RVCP-SUITE-REPORT: ACT RUN\n")
        fh.write(f"INPUT_LOG   : {uart_log}\n")
        fh.write(f"TOTAL_CASE_EXECUTIONS : {len(cases)}\n")
        fh.write(f"UNIQUE_CASES : {len(deduped_cases)}\n")
        fh.write(f"FINAL_PASS_CASES : {final_pass_cases}\n")
        fh.write(f"FINAL_FAIL_CASES : {final_fail_cases}\n\n")
        fh.write(f"EXPECTED_FAIL_CASES : {sum(1 for case in deduped_cases if case.final_verdict() == 'EXPECTED_FAIL')}\n\n")
        fh.write("PER-CASE:\n")
        for case in deduped_cases:
            verdict = case.final_verdict()
            rvcp_status = "PASSED" if verdict == "PASS" else verdict
            fh.write(f'RVCP-SUMMARY: Test File "{case.name}.S": {rvcp_status}\n')

    print(f"Wrote: {per_case_csv}")
    print(f"Wrote: {suite_summary}")
    print(f"Wrote: {act_suite_report}")
    print(f"Wrote: {per_case_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
