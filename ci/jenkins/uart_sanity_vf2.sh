#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
export REPO_ROOT="$repo_root"
export RUNNER_RESOLUTION_FILE="$repo_root/.jenkins_runner_resolution.txt"

export VF2_RUN_KIND="uart_sanity"
export VF2_RUN_ID_PREFIX="jenkins_uart_sanity"
export ACT_WORKDIR_NAME="work-vf2-jenkins-uart-sanity-priv"
export PRIV_GENERATOR_EXTENSIONS="ExceptionsF,ExceptionsS,ExceptionsSm,ExceptionsU,ExceptionsZc"
export INCLUDE_STATIC_PRIV_SUITES="false"
export EXPECTED_TEST_NAMES="ExceptionsF-00,ExceptionsS-00,ExceptionsSm-00,ExceptionsU-00,ExceptionsZc-00"

stage="${1:-}"
build_number="${BUILD_NUMBER:-manual}"
run_id="jenkins_uart_sanity_${build_number}"
state_root="$repo_root/logs/jenkins/uart_sanity/$run_id"
state_file="$state_root/state.env"
run_root="$repo_root/logs/runs/$run_id"

case "$stage" in
  preflight|prepare|spike)
    exec "$script_dir/weekly_vf2.sh" "$stage"
    ;;

  verify-runner-image)
    expected_build="$(git -C "$repo_root" rev-parse --short=12 HEAD)"
    image="$repo_root/cert_harness/build/vf2_jh7110/ACT_PRIV_M_OWN_ENV/uart_stream/boot_image.bin"
    PATH="/home/lpt-10xe/riscv64/bin:$PATH" \
      bash "$repo_root/cert_harness/tools/build_profile.sh" \
        --board vf2_jh7110 \
        --profile ACT_PRIV_M_OWN_ENV \
        --payload-transport uart_stream
    test -f "$image"
    mkdir -p "$state_root"
    sha256sum "$image" > "$state_root/uart_runner_image.sha256"
    printf '%s\n' "$expected_build" > "$state_root/expected_runner_build.txt"
    ;;

  run)
    test -f "$state_file"
    # shellcheck disable=SC1090
    source "$state_file"
    mapfile -t elfs < <(sed '/^[[:space:]]*$/d;/^[[:space:]]*#/d' "$PACK_LIST")
    if [[ "${#elfs[@]}" -eq 0 ]]; then
      echo "No runnable ELFs in $PACK_LIST" >&2
      exit 1
    fi
    expected_build="$(tr -d '[:space:]' < "$state_root/expected_runner_build.txt")"
    serial_dev="${SERIAL_DEV:-/dev/ttyUSB0}"
    python3 "$repo_root/cert_harness/uart_stream/run_elf_batch.py" \
      "${elfs[@]}" \
      --serial-dev "$serial_dev" \
      --run-dir "$run_root" \
      --ready-timeout "${UART_READY_TIMEOUT:-180}" \
      --result-timeout "${UART_RESULT_TIMEOUT:-600}" \
      --expect-runner-build "$expected_build" \
      --keep-going
    ;;

  finalize)
    mkdir -p "$state_root"
    {
      echo "run_id=$run_id"
      echo "completed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      echo "runner_commit=$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || true)"
      echo "transport=uart_stream"
      echo "sd_flash_per_run=no"
    } > "$state_root/jenkins_manifest.txt"
    if [[ -d "$run_root" ]]; then
      cp -f "$run_root/summary.json" "$state_root/summary.json" 2>/dev/null || true
      cp -f "$run_root/junit.xml" "$state_root/junit.xml" 2>/dev/null || true
    fi
    ;;

  *)
    echo "Usage: $0 {preflight|prepare|spike|verify-runner-image|run|finalize}" >&2
    exit 2
    ;;
esac
