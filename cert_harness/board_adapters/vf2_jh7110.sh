#!/usr/bin/env bash

board_flash_image() {
  local repo_root="$1"
  local image="$2"
  local sd_dev="$3"

  (cd "$repo_root" && ./vf2_act_flash.sh --image "$image" --sd-dev "$sd_dev")
}

board_write_pack() {
  local repo_root="$1"
  local pack="$2"
  local sd_dev="$3"

  (cd "$repo_root" && ./write_pack_to_sd_tail.sh "$pack" "$sd_dev")
}

board_capture_uart() {
  local repo_root="$1"
  local serial_dev="$2"
  local log_path="$3"
  local cooldown_sec="$4"
  local boot_cooldown_sec="$5"
  local boot_retries="$6"
  local boot_backoff_sec="$7"
  local cycle_delay="$8"
  local start_cycle="$9"

  local cmd=(
    "$repo_root/serial_auto_rebooter.sh"
    --serial-dev "$serial_dev"
    --log "$log_path"
    --cooldown "$cooldown_sec"
    --boot-cooldown "$boot_cooldown_sec"
    --boot-retries "$boot_retries"
    --boot-backoff "$boot_backoff_sec"
    --cycle-delay "$cycle_delay"
  )
  [[ "$start_cycle" -eq 1 ]] && cmd+=(--start-cycle)

  (cd "$repo_root" && "${cmd[@]}")
}
