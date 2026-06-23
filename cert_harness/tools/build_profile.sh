#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF2'
Usage:
  bash cert_harness/tools/build_profile.sh --board <name> --profile <name> [options]

Options:
  --board <name>          Board preset from cert_harness/boards/<name>.env.
  --platform <name>       Backward-compatible alias for --board.
  --profile <name>        Profile preset from cert_harness/profiles/<name>.env.
  --act-list <file>       Optional ACT ELF list. Builds and preserves act_pack.bin.
  --act-elf <file>        Optional single ACT ELF for embedded single-test builds.
  --riescue-elf <file>    Optional Riescue ELF override.
  --payload-transport <t> Override board payload transport for this build.
  --out-root <dir>        Output root (default: cert_harness/build).
  --no-package            Build firmware only; skip mkimage/truncate.
  --keep-make-outputs     Do not run make clean before building.
  -h, --help              Show this help.
EOF2
}

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
board=""
profile=""
act_list=""
act_elf=""
riescue_elf_override=""
payload_transport_override=""
out_root="cert_harness/build"
package_image=1
make_clean=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --board) board="${2:-}"; shift 2 ;;
    --platform) board="${2:-}"; shift 2 ;;
    --profile) profile="${2:-}"; shift 2 ;;
    --act-list) act_list="${2:-}"; shift 2 ;;
    --act-elf) act_elf="${2:-}"; shift 2 ;;
    --riescue-elf) riescue_elf_override="${2:-}"; shift 2 ;;
    --payload-transport) payload_transport_override="${2:-}"; shift 2 ;;
    --out-root) out_root="${2:-}"; shift 2 ;;
    --no-package) package_image=0; shift ;;
    --keep-make-outputs) make_clean=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$board" || -z "$profile" ]]; then
  usage >&2
  exit 1
fi

board_file="$repo_root/cert_harness/boards/${board}.env"
profile_file="$repo_root/cert_harness/profiles/${profile}.env"

if [[ ! -f "$board_file" ]]; then
  echo "ERROR: board preset not found: $board_file" >&2
  exit 1
fi
if [[ ! -f "$profile_file" ]]; then
  echo "ERROR: profile preset not found: $profile_file" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$board_file"
# shellcheck disable=SC1090
source "$profile_file"

payload_transport="${payload_transport_override:-${BOARD_PAYLOAD_TRANSPORT:-sd_tail_pack}}"
case "$payload_transport" in
  sd_tail_pack)
    BOARD_SD_ENABLE=1
    ;;
  embedded_pack)
    EXTERNAL_ONLY=0
    BOARD_SD_ENABLE=0
    ;;
  uart_stream|jtag_load|bootloader_ram)
    EXTERNAL_ONLY=1
    BOARD_SD_ENABLE=0
    ;;
  *)
    echo "ERROR: unsupported BOARD_PAYLOAD_TRANSPORT=$payload_transport" >&2
    exit 1
    ;;
esac

cd "$repo_root"

if [[ -n "$act_list" && -n "$act_elf" ]]; then
  echo "ERROR: use only one of --act-list or --act-elf." >&2
  exit 1
fi

if [[ -n "$riescue_elf_override" ]]; then
  RIESCUE_ELF="$riescue_elf_override"
fi

tool_prefix="${TOOLCHAIN_DIR:-}/${TOOLCHAIN_TRIPLE:-riscv64-unknown-elf}"
if [[ -x "${tool_prefix}-gcc" ]]; then
  CC_BIN="${tool_prefix}-gcc"
  OBJCOPY_BIN="${tool_prefix}-objcopy"
  OBJDUMP_BIN="${tool_prefix}-objdump"
  SIZE_BIN="${tool_prefix}-size"
else
  CC_BIN="${TOOLCHAIN_TRIPLE:-riscv64-unknown-elf}-gcc"
  OBJCOPY_BIN="${TOOLCHAIN_TRIPLE:-riscv64-unknown-elf}-objcopy"
  OBJDUMP_BIN="${TOOLCHAIN_TRIPLE:-riscv64-unknown-elf}-objdump"
  SIZE_BIN="${TOOLCHAIN_TRIPLE:-riscv64-unknown-elf}-size"
fi

fw_prefix="firmware_act"
if [[ "${PAYLOAD_PROFILE}" == "RIESCUE" ]]; then
  fw_prefix="firmware_riescue"
fi

run_id="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="$repo_root/$out_root/$board/$profile/$payload_transport"
mkdir -p "$out_dir"

if [[ "$make_clean" -eq 1 ]]; then
  make -f Makefile.act clean
fi

make_args=(
  -f Makefile.act
  "PAYLOAD_PROFILE=$PAYLOAD_PROFILE"
  "RUN_PRIV=$RUN_PRIV"
  "CC=$CC_BIN"
  "OBJCOPY=$OBJCOPY_BIN"
  "OBJDUMP=$OBJDUMP_BIN"
)
if [[ -n "${BOARD_LINKER_SCRIPT:-}" ]]; then
  make_args+=("LINKER_SCRIPT=$BOARD_LINKER_SCRIPT")
fi

board_cflags=()
for var in \
  BOARD_UART_BASE BOARD_UART_SIZE BOARD_CLINT_MSIP_BASE \
  BOARD_CLINT_MTIMECMP_BASE BOARD_CLINT_MTIME_ADDR \
  BOARD_RUNNER_HART_ID BOARD_MONITOR_HART_ID \
  BOARD_FIXED_TOHOST_ADDR BOARD_RAM_BASE BOARD_RAM_LIMIT \
  BOARD_EXT_PACK_ADDR BOARD_EXT_PACK_MAX_BYTES \
  BOARD_TEST_STACK_BYTES BOARD_SMODE_RUNTIME_STACK_BYTES \
  BOARD_TRAP_STACK_BYTES BOARD_STRAP_STACK_BYTES \
  BOARD_SDIO0_BASE BOARD_SDIO1_BASE BOARD_SD_BLOCK_SIZE BOARD_SD_ENABLE \
  BOARD_WDT_ENABLE BOARD_WDT_BASE BOARD_WDT_LOAD BOARD_WDT_CTRL \
  BOARD_WDT_LOCK BOARD_WDT_UNLOCK_KEY
do
  if [[ -n "${!var+x}" ]]; then
    board_cflags+=("-D$var=${!var}")
  fi
done

board_ldflags=()
if [[ -n "${BOARD_FW_LOAD_ADDR:-}" ]]; then
  board_ldflags+=("-Wl,--defsym=BOARD_FW_LOAD_ADDR=$BOARD_FW_LOAD_ADDR")
fi
if [[ -n "${BOARD_FW_STACK_BYTES:-}" ]]; then
  board_ldflags+=("-Wl,--defsym=BOARD_FW_STACK_BYTES=$BOARD_FW_STACK_BYTES")
fi

if [[ "${#board_cflags[@]}" -gt 0 ]]; then
  make_args+=("BOARD_CFLAGS=${board_cflags[*]}")
fi
if [[ "${#board_ldflags[@]}" -gt 0 ]]; then
  make_args+=("BOARD_LDFLAGS=${board_ldflags[*]}")
fi

for var in \
  EXTERNAL_ONLY RUNNER_ENABLE_LOWER_MODES RUNNER_ENABLE_TSBI \
  RUNNER_ENABLE_PRIVATE_SBI RUNNER_TOUCH_MENVCFG \
  RUNNER_ASSUME_RVA23_PRIV_FEATURES RUNNER_DELEG_POLICY \
  RUNNER_SMODE_CSR_POLICY RUNNER_USE_SV39 RUNNER_FAST_FAIL_RESET \
  RUNNER_TIMEOUT_FAST_RESET RUNNER_TEST_TIMEOUT_TICKS \
  RUNNER_RESET_DELAY_TICKS RUNNER_MAX_TEST_RETRIES RUNNER_VERBOSE_FLOW
do
  if [[ -n "${!var+x}" ]]; then
    make_args+=("$var=${!var}")
  fi
done

if [[ -n "${RIESCUE_ELF+x}" ]]; then
  make_args+=("RIESCUE_ELF=$RIESCUE_ELF")
fi
if [[ -n "$act_elf" ]]; then
  make_args+=("ACT_ELF=$act_elf")
fi

if [[ -n "$act_list" ]]; then
  make -f Makefile.act act_pack.bin "ACT_LIST=$act_list" \
    "CC=$CC_BIN" "OBJCOPY=$OBJCOPY_BIN" "OBJDUMP=$OBJDUMP_BIN"
  if [[ "$payload_transport" == "embedded_pack" ]]; then
    make_args+=("ACT_LIST=$act_list")
  fi
fi

make "${make_args[@]}"

boot_image="${BOARD_BOOT_IMAGE:-${BOOT_IMAGE:-uboot_part2_new.bin}}"
fit_source="${BOARD_FIT_SOURCE:-${FIT_SOURCE:-}}"
boot_image_pad_bytes="${BOARD_BOOT_IMAGE_PAD_BYTES:-${BOOT_IMAGE_PAD_BYTES:-}}"

if [[ "$package_image" -eq 1 && -n "$fit_source" ]]; then
  mkimage -f "$fit_source" "$boot_image"
  if [[ -n "$boot_image_pad_bytes" ]]; then
    truncate -s "$boot_image_pad_bytes" "$boot_image"
  fi
fi

cp -f "${fw_prefix}.elf" "$out_dir/firmware.elf"
cp -f "${fw_prefix}.bin" "$out_dir/firmware.bin"
cp -f "${fw_prefix}.dis" "$out_dir/firmware.dis"
if [[ -f act_pack.bin ]]; then
  cp -f act_pack.bin "$out_dir/act_pack.bin"
fi
if [[ "$package_image" -eq 1 && -f "$boot_image" ]]; then
  cp -f "$boot_image" "$out_dir/$boot_image"
  cp -f "$boot_image" "$out_dir/boot_image.bin"
fi

"$SIZE_BIN" "$out_dir/firmware.elf" > "$out_dir/size.txt"
(
  cd "$out_dir"
  sha256sum firmware.elf firmware.bin firmware.dis size.txt > sha256sums.txt
  if [[ -f act_pack.bin ]]; then
    sha256sum act_pack.bin >> sha256sums.txt
  fi
  if [[ -f boot_image.bin ]]; then
    sha256sum boot_image.bin >> sha256sums.txt
  fi
) 

git_commit="$(git rev-parse --short=12 HEAD 2>/dev/null || true)"
toolchain_version="$("$CC_BIN" --version | head -n 1)"
firmware_elf_bytes="$(stat -c '%s' "$out_dir/firmware.elf")"
firmware_bin_bytes="$(stat -c '%s' "$out_dir/firmware.bin")"
act_pack_bytes=""
boot_image_bytes=""
[[ -f "$out_dir/act_pack.bin" ]] && act_pack_bytes="$(stat -c '%s' "$out_dir/act_pack.bin")"
[[ -f "$out_dir/boot_image.bin" ]] && boot_image_bytes="$(stat -c '%s' "$out_dir/boot_image.bin")"

cat > "$out_dir/manifest.json" <<EOF2
{
  "run_id": "$run_id",
  "board": "$board",
  "board_description": "${BOARD_DESCRIPTION:-${PLATFORM_DESCRIPTION:-}}",
  "board_linker_script": "${BOARD_LINKER_SCRIPT:-link.ld}",
  "board_fw_load_addr": "${BOARD_FW_LOAD_ADDR:-}",
  "board_ram_base": "${BOARD_RAM_BASE:-}",
  "board_ram_limit": "${BOARD_RAM_LIMIT:-}",
  "board_uart_base": "${BOARD_UART_BASE:-}",
  "board_clint_msip_base": "${BOARD_CLINT_MSIP_BASE:-}",
  "board_clint_mtime_addr": "${BOARD_CLINT_MTIME_ADDR:-}",
  "board_sdio1_base": "${BOARD_SDIO1_BASE:-}",
  "board_sd_enable": "${BOARD_SD_ENABLE:-}",
  "board_ext_pack_addr": "${BOARD_EXT_PACK_ADDR:-}",
  "payload_transport": "$payload_transport",
  "profile": "$profile",
  "profile_status": "${PROFILE_STATUS:-}",
  "profile_notes": "${PROFILE_NOTES:-}",
  "git_commit": "$git_commit",
  "toolchain": "$toolchain_version",
  "payload_profile": "$PAYLOAD_PROFILE",
  "run_priv": "$RUN_PRIV",
  "external_only": "${EXTERNAL_ONLY:-}",
  "act_list": "$act_list",
  "act_elf": "$act_elf",
  "riescue_elf": "${RIESCUE_ELF:-}",
  "firmware_elf_bytes": "$firmware_elf_bytes",
  "firmware_bin_bytes": "$firmware_bin_bytes",
  "act_pack_bytes": "$act_pack_bytes",
  "boot_image_bytes": "$boot_image_bytes",
  "artifact_dir": "$out_dir"
}
EOF2

echo "Built profile artifacts:"
echo "  $out_dir"
echo "Manifest:"
echo "  $out_dir/manifest.json"
