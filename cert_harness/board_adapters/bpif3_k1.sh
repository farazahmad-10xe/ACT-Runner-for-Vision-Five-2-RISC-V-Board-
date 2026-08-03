#!/usr/bin/env bash

# Banana Pi BPI-F3 / SpacemiT K1 board adapter.

board_flash_image() {
  local repo_root="$1"
  local image="$2"
  local sd_dev="$3"

  : "$repo_root"

  # GPT partition 3 on the SD card is "opensbi" (fsbl=1, env=2, opensbi=3, uboot=4).
  local part_dev="${sd_dev}3"

  echo "Flashing $image to opensbi partition $part_dev" >&2
  sudo dd if="$image" of="$part_dev" bs=512 status=progress conv=fsync
}

board_write_pack() {
  local repo_root="$1"
  local pack="$2"
  local sd_dev="$3"

  (cd "$repo_root" && ./write_pack_to_sd_tail.sh "$pack" "$sd_dev")
}

board_capture_uart() {
  local serial_dev="$2"
  local log_path="$3"

  mkdir -p "$(dirname "$log_path")"
  picocom -b 115200 --flow n -q --logfile "$log_path" "$serial_dev"
}
