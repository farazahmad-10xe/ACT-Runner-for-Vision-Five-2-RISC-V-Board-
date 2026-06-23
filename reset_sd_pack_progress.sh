#!/usr/bin/env bash
set -euo pipefail

SD_DEV="${1:-/dev/sda}"

if [[ ! -b "$SD_DEV" ]]; then
  echo "ERROR: not a block device: $SD_DEV" >&2
  exit 1
fi

sudo python3 - "$SD_DEV" <<'PY'
import struct
import subprocess
import sys

dev = sys.argv[1]
blocks = int(subprocess.check_output(["blockdev", "--getsz", dev], text=True).strip())
footer_lba = blocks - 1

with open(dev, "r+b", buffering=0) as f:
    f.seek(footer_lba * 512)
    footer = bytearray(f.read(512))
    if len(footer) != 512:
        raise SystemExit("ERROR: could not read SD footer block")

    magic, version, start_lba, num_blocks, reserved = struct.unpack_from("<IIQII", footer, 0)
    if magic != 0x464B5041 or version != 1:
        raise SystemExit(
            f"ERROR: SD tail footer magic/version not found at LBA {footer_lba}: "
            f"magic=0x{magic:08x} version={version}"
        )

    struct.pack_into("<I", footer, 20, 0)
    f.seek(footer_lba * 512)
    f.write(footer)

print(
    f"Reset SD pack progress: dev={dev} footer_lba={footer_lba} "
    f"start_lba={start_lba} blocks={num_blocks} next_index=0"
)
PY

sync
