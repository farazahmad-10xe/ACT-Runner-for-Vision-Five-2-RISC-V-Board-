#!/usr/bin/env python3
"""Send one ELF to a RUNNER_UART_STREAM firmware instance over UART."""

from __future__ import annotations

import argparse
import os
import struct
import sys
import time
import zlib
from pathlib import Path

import serial


MAGIC = 0x31534655  # Little-endian bytes: UFS1
VERSION = 1
NAME_BYTES = 64
HEADER = struct.Struct("<IIQII64s")
READY_MARKER = b"[UART_STREAM] READY"
HEADER_OK_MARKER = b"[UART_STREAM] HEADER_OK"
RX_OK_MARKER = b"[UART_STREAM] RX_OK"
DONE_MARKER = b"[UART_STREAM] DONE"
ERROR_MARKER = b"[UART_STREAM] ERROR"


class Capture:
    def __init__(self, path: Path | None) -> None:
        self._file = None
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)
            self._file = path.open("wb")

    def write(self, data: bytes) -> None:
        if not data:
            return
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        if self._file is not None:
            self._file.write(data)
            self._file.flush()

    def close(self) -> None:
        if self._file is not None:
            self._file.close()


def make_header(name: str, payload: bytes) -> tuple[bytes, int]:
    name_raw = name.encode("utf-8", errors="replace")[:NAME_BYTES]
    if not name_raw:
        raise ValueError("ELF name must not be empty")
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    padded_name = name_raw + bytes(NAME_BYTES - len(name_raw))
    return (
        HEADER.pack(MAGIC, VERSION, len(payload), crc, len(name_raw), padded_name),
        crc,
    )


def read_until(
    port: serial.Serial,
    capture: Capture,
    wanted: bytes,
    timeout_seconds: float,
) -> bytes:
    deadline = time.monotonic() + timeout_seconds
    window = bytearray()
    while time.monotonic() < deadline:
        data = port.read(max(1, port.in_waiting))
        if not data:
            continue
        capture.write(data)
        window.extend(data)
        if ERROR_MARKER in window:
            raise RuntimeError("target reported UART stream protocol error")
        if wanted in window:
            return bytes(window)
        if len(window) > 65536:
            del window[:-32768]
    raise TimeoutError(f"timed out waiting for {wanted.decode(errors='replace')}")


def read_marker_line(
    port: serial.Serial,
    capture: Capture,
    marker: bytes,
    timeout_seconds: float,
) -> bytes:
    """Wait for marker and the newline that terminates the same target line."""
    data = read_until(port, capture, marker, timeout_seconds)
    marker_end = data.find(marker) + len(marker)
    if b"\n" not in data[marker_end:]:
        data += read_until(port, capture, b"\n", 5.0)
    return data


def drain(port: serial.Serial, capture: Capture) -> None:
    waiting = port.in_waiting
    if waiting:
        capture.write(port.read(waiting))


def send_payload(
    port: serial.Serial,
    capture: Capture,
    payload: bytes,
    chunk_size: int,
) -> None:
    sent = 0
    next_progress = 10
    while sent < len(payload):
        end = min(sent + chunk_size, len(payload))
        port.write(payload[sent:end])
        sent = end
        drain(port, capture)
        percent = (sent * 100) // len(payload)
        if percent >= next_progress or sent == len(payload):
            print(
                f"\n[HOST_UART_STREAM] sent={sent}/{len(payload)} ({percent}%)",
                flush=True,
            )
            next_progress = percent + 10
    port.flush()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Wait for a UART-stream runner and send one ELF."
    )
    parser.add_argument("elf", type=Path)
    parser.add_argument("--serial-dev", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--name", help="Case name; defaults to ELF stem")
    parser.add_argument("--log", type=Path, help="Raw target UART capture")
    parser.add_argument("--ready-timeout", type=float, default=180.0)
    parser.add_argument("--result-timeout", type=float, default=300.0)
    parser.add_argument("--chunk-size", type=int, default=4096)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate and display the frame without opening UART",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    payload = args.elf.read_bytes()
    name = args.name or args.elf.stem
    header, crc = make_header(name, payload)

    print(
        f"[HOST_UART_STREAM] elf={args.elf} name={name!r} "
        f"bytes={len(payload)} crc32=0x{crc:08x} header_bytes={len(header)}"
    )
    if len(payload) < 64 or payload[:4] != b"\x7fELF":
        raise SystemExit("input is not an ELF file")
    if args.chunk_size <= 0:
        raise SystemExit("--chunk-size must be positive")
    if args.dry_run:
        print(f"[HOST_UART_STREAM] frame_bytes={len(header) + len(payload)}")
        return 0

    capture = Capture(args.log)
    try:
        with serial.Serial(
            args.serial_dev,
            args.baud,
            timeout=0.1,
            write_timeout=30.0,
            exclusive=True,
        ) as port:
            print(
                f"[HOST_UART_STREAM] waiting for target on {args.serial_dev}; "
                "power on or reset the board now",
                flush=True,
            )
            read_marker_line(port, capture, READY_MARKER, args.ready_timeout)
            written = port.write(header)
            port.flush()
            if written != len(header):
                raise RuntimeError(
                    f"short UART header write: {written}/{len(header)} bytes"
                )
            print(f"\n[HOST_UART_STREAM] header_sent={written}", flush=True)
            read_marker_line(port, capture, HEADER_OK_MARKER, 10.0)
            send_payload(port, capture, payload, args.chunk_size)
            read_until(port, capture, RX_OK_MARKER, 30.0)
            read_marker_line(port, capture, DONE_MARKER, args.result_timeout)
            drain(port, capture)
    finally:
        capture.close()

    print("\n[HOST_UART_STREAM] completed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
