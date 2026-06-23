#!/usr/bin/env python3
import argparse
import os
import shutil
import struct
import subprocess
from pathlib import Path


ECALL = b"\x73\x00\x00\x00"
NOP = b"\x13\x00\x00\x00"
PT_LOAD = 1


def find_symbol(path: Path, name: str) -> int:
    out = subprocess.check_output(["riscv64-unknown-elf-nm", "-n", str(path)], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError(f"{path}: symbol {name} not found")


def vaddr_to_file_offset(blob: bytes, vaddr: int) -> int:
    if blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
        raise RuntimeError("expected ELF64 little-endian file")

    e_phoff = struct.unpack_from("<Q", blob, 32)[0]
    e_phentsize = struct.unpack_from("<H", blob, 54)[0]
    e_phnum = struct.unpack_from("<H", blob, 56)[0]

    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, _p_flags = struct.unpack_from("<II", blob, off)
        if p_type != PT_LOAD:
            continue
        p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz, _p_align = struct.unpack_from("<QQQQQQ", blob, off + 8)
        if p_vaddr <= vaddr < p_vaddr + p_filesz:
            return p_offset + (vaddr - p_vaddr)

    raise RuntimeError(f"vaddr 0x{vaddr:x} not inside a loadable file segment")


def patch_one(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

    halt_fail = find_symbol(dst, "rvmodel_halt_fail")
    ecall_vaddr = halt_fail + 0x48
    blob = bytearray(dst.read_bytes())
    file_off = vaddr_to_file_offset(blob, ecall_vaddr)

    old = bytes(blob[file_off:file_off + 4])
    if old != ECALL:
        raise RuntimeError(
            f"{src}: expected ecall at rvmodel_halt_fail+0x48 "
            f"(vaddr=0x{ecall_vaddr:x}, file_off=0x{file_off:x}), found {old.hex()}"
        )

    blob[file_off:file_off + 4] = NOP
    dst.write_bytes(blob)
    print(f"patched {dst} vaddr=0x{ecall_vaddr:x} file_off=0x{file_off:x}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Patch ACT rvmodel_halt_fail to stop repeated ecall failure spam.")
    ap.add_argument("--src-root", default="tests/priv")
    ap.add_argument("--out-root", default="tests/priv_no_fail_ecall")
    args = ap.parse_args()

    src_root = Path(args.src_root)
    out_root = Path(args.out_root)
    count = 0
    for src in sorted(src_root.glob("*/*.elf")):
        rel = src.relative_to(src_root)
        patch_one(src, out_root / rel)
        count += 1

    print(f"patched_count={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
