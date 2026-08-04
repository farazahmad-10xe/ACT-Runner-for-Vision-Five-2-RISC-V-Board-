#!/usr/bin/env bash
set -euo pipefail

image=""
sd_dev="/dev/sda"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --image) image="${2:-}"; shift 2 ;;
    --sd-dev) sd_dev="${2:-}"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 --image <bpif3_opensbi.itb> [--sd-dev /dev/sdX]"
      exit 0
      ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

[[ -n "$image" && -f "$image" ]] || { echo "BPI-F3 boot image is missing: $image" >&2; exit 1; }
[[ -b "$sd_dev" ]] || { echo "Not a block device: $sd_dev" >&2; exit 1; }
case "$sd_dev" in
  /dev/mmcblk*|/dev/nvme*) partition="${sd_dev}p3" ;;
  *) partition="${sd_dev}3" ;;
esac
[[ -b "$partition" ]] || { echo "BPI-F3 OpenSBI partition is missing: $partition" >&2; exit 1; }

umount "$partition" 2>/dev/null || true
sync
echo "Writing BPI-F3 ACT OpenSBI FIT to $partition"
dd if="$image" of="$partition" bs=1M conv=fsync status=progress
sync

image_bytes="$(stat -c '%s' "$image")"
verify="$(mktemp /tmp/bpif3-opensbi-verify.XXXXXX)"
trap 'rm -f "$verify"' EXIT
dd if="$partition" of="$verify" bs=1 count="$image_bytes" status=none
cmp -s "$image" "$verify"
echo "BPI-F3 OpenSBI FIT read-back verification passed."
