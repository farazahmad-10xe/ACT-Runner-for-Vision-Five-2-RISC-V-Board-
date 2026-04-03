#!/usr/bin/env python3
"""Build a packed ACT payload from a list of ELF files.

Format (little-endian):
  header: magic(u32)=0x4b504341 ("ACPK"), version(u32)=1, count(u32), reserved(u32)=0
  entries[count]: name[64], offset(u64), size(u64)
  payload blobs concatenated
"""

from __future__ import annotations

import argparse
import os
import struct
from pathlib import Path

PACK_MAGIC = 0x4B504341
PACK_VERSION = 1
NAME_SIZE = 64
HEADER_FMT = "<IIII"
ENTRY_FMT = f"<{NAME_SIZE}sQQ"


def read_list(list_path: Path) -> list[Path]:
    out: list[Path] = []
    for line in list_path.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        out.append(Path(s).expanduser())
    return out


def encode_name(path: Path) -> bytes:
    b = path.stem.encode("ascii", errors="ignore")
    if not b:
        b = path.name.encode("ascii", errors="ignore") or b"unnamed"
    return b[: NAME_SIZE - 1].ljust(NAME_SIZE, b"\0")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", required=True, help="File with one ELF path per line")
    ap.add_argument("--out", required=True, help="Output packed binary")
    args = ap.parse_args()

    list_path = Path(args.list)
    out_path = Path(args.out)

    elfs = read_list(list_path)
    if not elfs:
        raise SystemExit("No ELF paths found in list file")

    for p in elfs:
        if not p.is_file():
            raise SystemExit(f"Missing ELF file: {p}")

    count = len(elfs)
    header_size = struct.calcsize(HEADER_FMT)
    entry_size = struct.calcsize(ENTRY_FMT)
    data_offset = header_size + (count * entry_size)

    entries: list[tuple[bytes, int, int]] = []
    blobs: list[bytes] = []

    cur = data_offset
    for p in elfs:
        blob = p.read_bytes()
        entries.append((encode_name(p), cur, len(blob)))
        blobs.append(blob)
        cur += len(blob)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(struct.pack(HEADER_FMT, PACK_MAGIC, PACK_VERSION, count, 0))
        for name, off, size in entries:
            f.write(struct.pack(ENTRY_FMT, name, off, size))
        for blob in blobs:
            f.write(blob)

    print(f"Packed {count} ELF files -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
