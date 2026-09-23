#!/usr/bin/env python3
"""Run multiple ELFs through the one-ELF UART runner with power isolation."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


DONE_RE = re.compile(
    rb"\[UART_STREAM\] DONE name=([^\r\n ]+) "
    rb"status=(PASS|FAIL|TIMEOUT|ERROR) tohost=(0x[0-9a-fA-F]+)"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Power-cycle a board and send one UART ELF per boot."
    )
    parser.add_argument("elfs", nargs="+", type=Path)
    parser.add_argument("--serial-dev", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--tuya-config", type=Path, default=Path("devices.json"))
    parser.add_argument("--device-name", help="Select a named devices.json entry")
    parser.add_argument("--cycle-delay", type=float, default=3.0)
    parser.add_argument("--ready-timeout", type=float, default=180.0)
    parser.add_argument("--result-timeout", type=float, default=300.0)
    parser.add_argument(
        "--no-power-cycle",
        action="store_true",
        help="Prompt for a manual reset before each ELF",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="Continue after a transport error",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate inputs and print actions without UART or power changes",
    )
    return parser.parse_args()


def safe_name(path: Path) -> str:
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", path.stem).strip("._")
    return name or "elf"


def load_tuya_env(config: Path, device_name: str | None) -> dict[str, str]:
    env = os.environ.copy()
    required = ("TUYA_DEVICE_ID", "TUYA_DEVICE_IP", "TUYA_LOCAL_KEY")
    if all(env.get(key) for key in required):
        return env

    devices = json.loads(config.read_text(encoding="utf-8"))
    if not isinstance(devices, list) or not devices:
        raise ValueError(f"{config} must contain a non-empty device list")
    if device_name:
        matches = [item for item in devices if item.get("name") == device_name]
        if len(matches) != 1:
            raise ValueError(
                f"expected one device named {device_name!r}, found {len(matches)}"
            )
        device = matches[0]
    else:
        device = devices[0]

    mapping = {
        "TUYA_DEVICE_ID": device.get("id"),
        "TUYA_DEVICE_IP": device.get("ip"),
        "TUYA_LOCAL_KEY": device.get("key"),
        "TUYA_VERSION": str(device.get("version", "3.4")),
    }
    for key, value in mapping.items():
        if value and not env.get(key):
            env[key] = value
    missing = [key for key in required if not env.get(key)]
    if missing:
        raise ValueError(f"missing Tuya fields: {', '.join(missing)}")
    return env


def parse_done(log_path: Path) -> tuple[str, str, str] | None:
    matches = list(DONE_RE.finditer(log_path.read_bytes()))
    if not matches:
        return None
    match = matches[-1]
    return tuple(part.decode("ascii", errors="replace") for part in match.groups())


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    sender = repo_root / "cert_harness/uart_stream/send_elf.py"
    power_ctl = repo_root / "tuya_plug_ctl.py"
    elfs = [path.resolve() for path in args.elfs]

    for elf in elfs:
        data = elf.read_bytes()
        if len(data) < 64 or data[:4] != b"\x7fELF":
            raise SystemExit(f"not an ELF file: {elf}")

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = (args.run_dir or Path(f"logs/runs/uart_stream_batch_{stamp}")).resolve()
    print(f"[UART_BATCH] cases={len(elfs)} run_dir={run_dir}")
    for index, elf in enumerate(elfs, 1):
        print(f"[UART_BATCH] plan index={index} elf={elf}")
    if args.dry_run:
        print("[UART_BATCH] dry-run complete; no power or UART action performed")
        return 0

    run_dir.mkdir(parents=True, exist_ok=True)
    power_env = None
    if not args.no_power_cycle:
        power_env = load_tuya_env(args.tuya_config, args.device_name)

    results: list[dict[str, object]] = []
    for index, elf in enumerate(elfs, 1):
        case_name = safe_name(elf)
        log_path = run_dir / f"{index:04d}_{case_name}.uart.log"
        print(f"\n[UART_BATCH] START index={index}/{len(elfs)} name={case_name}")

        if args.no_power_cycle:
            input("Power-cycle/reset the board, then press Enter to continue: ")
        else:
            print(f"[UART_BATCH] power_cycle delay={args.cycle_delay}s")
            subprocess.run(
                [
                    sys.executable,
                    str(power_ctl),
                    "cycle",
                    "--delay",
                    str(args.cycle_delay),
                ],
                cwd=repo_root,
                env=power_env,
                check=True,
            )

        started = time.monotonic()
        command = [
            sys.executable,
            str(sender),
            str(elf),
            "--serial-dev",
            args.serial_dev,
            "--baud",
            str(args.baud),
            "--log",
            str(log_path),
            "--ready-timeout",
            str(args.ready_timeout),
            "--result-timeout",
            str(args.result_timeout),
        ]
        process = subprocess.run(command, cwd=repo_root, check=False)
        elapsed = round(time.monotonic() - started, 3)
        done = parse_done(log_path) if log_path.exists() else None
        if done:
            target_name, status, tohost = done
        else:
            target_name, status, tohost = case_name, "TRANSPORT_ERROR", ""
        result = {
            "index": index,
            "elf": str(elf),
            "name": target_name,
            "status": status,
            "tohost": tohost,
            "sender_returncode": process.returncode,
            "elapsed_seconds": elapsed,
            "uart_log": str(log_path),
        }
        results.append(result)
        print(
            f"[UART_BATCH] RESULT index={index} name={target_name} "
            f"status={status} tohost={tohost or 'unavailable'} elapsed={elapsed}s"
        )
        if (process.returncode != 0 or done is None) and not args.keep_going:
            print("[UART_BATCH] stopping after transport error", file=sys.stderr)
            break

    counts: dict[str, int] = {}
    for result in results:
        status = str(result["status"])
        counts[status] = counts.get(status, 0) + 1
    summary = {
        "planned": len(elfs),
        "completed": len(results),
        "counts": counts,
        "results": results,
    }
    summary_path = run_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"\n[UART_BATCH] SUMMARY {json.dumps(counts, sort_keys=True)}")
    print(f"[UART_BATCH] summary_file={summary_path}")
    return 1 if counts.get("TRANSPORT_ERROR", 0) else 0


if __name__ == "__main__":
    raise SystemExit(main())
