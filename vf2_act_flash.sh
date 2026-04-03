#!/usr/bin/env bash
set -euo pipefail

# Defaults (can be overridden by args)
WORKDIR="$HOME/vf2_mmode_fw"
SD_DEV="/dev/sda"
OFFSET_BLOCKS=$((0x2000))      # SPL reads from here
COUNT_BLOCKS=$((0x2000))       # 0x2000 blocks = 4 MiB

usage() {
  cat <<'EOF2'
Usage:
  ./vf2_act_flash.sh --image <uboot_part2_new.bin> [options]
  ./vf2_act_flash.sh --build-elf <test.elf> [options]

Options:
  --image <file>        Flash an already-built 4MiB image file.
  --build-elf <file>    Build firmware from test ELF, build FIT image, then flash.
  --sd-dev <device>     SD block device (default: /dev/sda).
  --offset-blocks <n>   Write offset in 512B blocks (default: 0x2000).
  --count-blocks <n>    Write count in 512B blocks (default: 0x2000).
  -h, --help            Show this help.
EOF2
}

IMAGE_FILE=""
ACT_ELF=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --image) IMAGE_FILE="${2:-}"; shift 2 ;;
    --build-elf) ACT_ELF="${2:-}"; shift 2 ;;
    --sd-dev) SD_DEV="${2:-}"; shift 2 ;;
    --offset-blocks) OFFSET_BLOCKS="${2:-}"; shift 2 ;;
    --count-blocks) COUNT_BLOCKS="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -n "$IMAGE_FILE" && -n "$ACT_ELF" ]]; then
  echo "ERROR: use only one of --image or --build-elf." >&2
  exit 1
fi
if [[ -z "$IMAGE_FILE" && -z "$ACT_ELF" ]]; then
  echo "ERROR: provide --image or --build-elf." >&2
  exit 1
fi

cd "$WORKDIR"

echo "==> Sanity checks..."
test -b "$SD_DEV" || { echo "ERROR: SD device not a block device: $SD_DEV"; exit 1; }

if [[ -n "$ACT_ELF" ]]; then
  test -f "$ACT_ELF" || { echo "ERROR: ACT ELF not found: $WORKDIR/$ACT_ELF"; exit 1; }
  echo "==> Clean..."
  make -f Makefile.act clean
  echo "==> Build runner firmware with embedded ACT ELF..."
  make -f Makefile.act ACT_ELF="$ACT_ELF"
  echo "==> Build FIT image..."
  mkimage -f vf2_act4.its uboot_part2_new.bin
  echo "==> Pad FIT image to 4 MiB (required by SPL read size)..."
  truncate -s 4M uboot_part2_new.bin
  IMAGE_FILE="uboot_part2_new.bin"
fi

test -f "$IMAGE_FILE" || { echo "ERROR: image file not found: $IMAGE_FILE"; exit 1; }

echo "==> Unmount SD partitions (ignore errors)..."
sudo umount "${SD_DEV}3" "${SD_DEV}4" 2>/dev/null || true
sync

echo "==> Flash to SD (offset 0x2000 blocks)..."
sudo dd if="$IMAGE_FILE" of="$SD_DEV" bs=512 seek="$OFFSET_BLOCKS" count="$COUNT_BLOCKS" conv=fsync
sync

echo "==> Verify (read back and inspect FIT header)..."
sudo dd if="$SD_DEV" bs=512 skip="$OFFSET_BLOCKS" count="$COUNT_BLOCKS" of=part2_full.bin
dumpimage -l part2_full.bin

echo "==> DONE. Insert SD in VF2 and boot."
