#!/usr/bin/env bash
set -euo pipefail

PACK_FILE="${1:-./act_pack.bin}"
SD_DEV="${2:-/dev/sda}"
RESERVE_BLOCKS="${RESERVE_BLOCKS:-2048}"   # keep 1MiB gap before footer

if [[ ! -f "$PACK_FILE" ]]; then
  echo "ERROR: pack file not found: $PACK_FILE" >&2
  exit 1
fi
if [[ ! -b "$SD_DEV" ]]; then
  echo "ERROR: not a block device: $SD_DEV" >&2
  exit 1
fi

pack_bytes="$(stat -c '%s' "$PACK_FILE")"
pack_blocks="$(( (pack_bytes + 511) / 512 ))"

# Needs root for reliable device queries + writes.
total_blocks="$(sudo blockdev --getsz "$SD_DEV")"

# Detect the end of current partitions to avoid overlap.
last_used_block=0
dev_base="$(basename "$SD_DEV")"
for p in /sys/class/block/"$dev_base"/"$dev_base"*; do
  [[ -d "$p" ]] || continue
  part_name="$(basename "$p")"
  [[ "$part_name" == "$dev_base" ]] && continue
  if [[ -f "$p/start" && -f "$p/size" ]]; then
    start="$(cat "$p/start")"
    size_blocks="$(cat "$p/size")"
    end="$(( start + size_blocks ))"
    if (( end > last_used_block )); then
      last_used_block="$end"
    fi
  fi
done

# Layout: [ ... pack ... ][reserve][footer(1 block)]
footer_lba="$(( total_blocks - 1 ))"
start_lba="$(( footer_lba - RESERVE_BLOCKS - pack_blocks ))"

if (( start_lba <= 0 )); then
  echo "ERROR: card too small for pack placement." >&2
  exit 1
fi
if (( start_lba < last_used_block )); then
  echo "ERROR: computed start_lba overlaps existing partition usage." >&2
  echo "  last_used_block=${last_used_block} start_lba=${start_lba}" >&2
  echo "Use a larger/dedicated SD card or shrink partitions." >&2
  exit 1
fi

echo "Writing pack to SD tail"
echo "  device       : $SD_DEV"
echo "  pack file    : $PACK_FILE"
echo "  pack bytes   : $pack_bytes"
echo "  pack blocks  : $pack_blocks"
echo "  start lba    : $start_lba"
echo "  footer lba   : $footer_lba"

sudo dd if="$PACK_FILE" of="$SD_DEV" bs=512 seek="$start_lba" conv=fsync status=progress

footer_bin="$(mktemp /tmp/act_pack_footer.XXXXXX.bin)"
python3 - "$footer_bin" "$start_lba" "$pack_blocks" <<'PY'
import struct
import sys

out = sys.argv[1]
start_lba = int(sys.argv[2])
num_blocks = int(sys.argv[3])
magic = 0x464B5041  # 'APKF'
version = 1

payload = struct.pack('<IIQII', magic, version, start_lba, num_blocks, 0)
payload += b'\x00' * (512 - len(payload))
with open(out, 'wb') as f:
    f.write(payload)
PY

sudo dd if="$footer_bin" of="$SD_DEV" bs=512 seek="$footer_lba" count=1 conv=fsync status=none
rm -f "$footer_bin"
sync

echo "Done. Runner will auto-discover pack via footer at LBA ${footer_lba}."
