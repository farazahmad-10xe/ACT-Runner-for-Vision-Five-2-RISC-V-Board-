#!/usr/bin/env bash
set -euo pipefail

repo_root="${REPO_ROOT:-/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified}"
cd "$repo_root"

stage="${1:-}"
build_number="${BUILD_NUMBER:-manual}"
run_id="jenkins_weekly_${build_number}"
state_root="$repo_root/logs/jenkins/weekly/$run_id"
state_file="$state_root/state.env"
priv_test_dir="$state_root/all_priv_tests"
generated_test_root="$state_root/generated_tests"
missing_report="$state_root/reference_failed_no_hardware_elf.txt"
reference_status="$state_root/sail_reference_status.tsv"
priv_source_roots="$state_root/priv_source_roots.txt"
sd_dev="${SD_DEV:-/dev/sda}"
serial_dev="${SERIAL_DEV:-/dev/ttyUSB0}"
capture_timeout="${CAPTURE_TIMEOUT:-10800}"
sail_bin="${SAIL_BIN:-/home/lpt-10xe/riscv-sail-0.13/bin/sail_riscv_sim}"
sail_expected_version="${SAIL_EXPECTED_VERSION:-0.13}"
act_expected_revision="${ACT_REVISION:-0683245155d659437be40d353cedf26fb0d56f1c}"

all_extensions="all"
act_root="$repo_root/external/riscv-arch-test"
act_config="config/cores/sifive_u74/test_config.local.yaml"
u74_yaml="$act_root/config/cores/sifive_u74/visionfive2-rv64gc.yaml"
u74_sail_json="$act_root/config/cores/sifive_u74/sail.json"
act_workdir="work-vf2-jenkins-all-priv"
dut_name="visionfive2-rv64gc"
artifact_root="$act_root/$act_workdir/$dut_name/build/priv"
reference_root="$repo_root/logs/reference-model-runs/$run_id"
pack_list="$repo_root/logs/runs/$run_id/act_elfs.list"
hardware_artifacts="$repo_root/cert_harness/build/vf2_jh7110/ACT_PRIV_M_OWN_ENV/sd_tail_pack"

mkdir -p "$state_root"

write_state() {
  local expected_cases="0"
  if [[ -f "$pack_list" ]]; then
    expected_cases="$(sed '/^[[:space:]]*$/d;/^[[:space:]]*#/d' "$pack_list" | wc -l | tr -d ' ')"
  fi
  {
    printf 'RUN_ID=%q\n' "$run_id"
    printf 'ACT_WORKDIR=%q\n' "$act_workdir"
    printf 'ACT_CONFIG=%q\n' "$act_config"
    printf 'U74_YAML=%q\n' "$u74_yaml"
    printf 'U74_SAIL_JSON=%q\n' "$u74_sail_json"
    printf 'ARTIFACT_ROOT=%q\n' "$artifact_root"
    printf 'REFERENCE_ROOT=%q\n' "$reference_root"
    printf 'PACK_LIST=%q\n' "$pack_list"
    printf 'HARDWARE_ARTIFACTS=%q\n' "$hardware_artifacts"
    printf 'PRIV_TEST_DIR=%q\n' "$priv_test_dir"
    printf 'MISSING_REPORT=%q\n' "$missing_report"
    printf 'REFERENCE_STATUS=%q\n' "$reference_status"
    printf 'PRIV_SOURCE_ROOTS=%q\n' "$priv_source_roots"
    printf 'EXPECTED_CASES=%q\n' "$expected_cases"
    printf 'ACT_REVISION=%q\n' "$act_expected_revision"
  } > "$state_file"
}

load_state() {
  [[ -f "$state_file" ]] || { echo "Missing state file: $state_file" >&2; exit 1; }
  # shellcheck disable=SC1090
  source "$state_file"
}

case "$stage" in
  preflight)
    test -f Jenkinsfile
    test -f ci/jenkins/stage_priv_tests.py
    test -f "$act_root/$act_config"
    test -f "$u74_yaml"
    test -f "$u74_sail_json"
    test -x tools/act_agent/run_vf2_pack.py
    test -f tools/act_agent/run_reference_elf.py
    test -x cert_harness/tools/run_profile.sh
    command -v uv
    command -v make
    command -v mkimage
    command -v zip
    command -v curl
    command -v riscv64-unknown-elf-gcc
    test -x "$sail_bin"
    if ! resolved_act_revision="$(git -C "$act_root" rev-parse --verify "${act_expected_revision}^{commit}" 2>/dev/null)"; then
      echo "Configured ACT_REVISION is not available locally: $act_expected_revision" >&2
      echo "Run the ACT update-validation job or fetch the reviewed VF2 branch before starting hardware execution." >&2
      exit 1
    fi
    actual_act_revision="$(git -C "$act_root" rev-parse HEAD)"
    if [[ "$actual_act_revision" != "$resolved_act_revision" ]]; then
      echo "ACT revision mismatch; refusing a non-reproducible hardware run." >&2
      echo "Expected: $resolved_act_revision" >&2
      echo "Actual:   $actual_act_revision" >&2
      exit 1
    fi
    if ! git -C "$act_root" diff --quiet --ignore-submodules -- ||
       ! git -C "$act_root" diff --cached --quiet --ignore-submodules --; then
      echo "Tracked ACT files differ from commit $actual_act_revision." >&2
      echo "Commit/revert those changes and review a new ACT_REVISION before running hardware." >&2
      git -C "$act_root" status --short --untracked-files=no >&2
      exit 1
    fi
    resolved_sail="$(command -v sail_riscv_sim)"
    if [[ "$(readlink -f "$resolved_sail")" != "$(readlink -f "$sail_bin")" ]]; then
      echo "Jenkins PATH resolves Sail to $resolved_sail, expected $sail_bin" >&2
      exit 1
    fi
    sail_version="$("$sail_bin" --version | head -n 1 | tr -d '\r')"
    if [[ "$sail_version" != "$sail_expected_version" ]]; then
      echo "Sail version mismatch: found '$sail_version', expected '$sail_expected_version'" >&2
      exit 1
    fi
    act_required_sail="$(
      sed -n 's/^REQUIRED_SAIL_VERSION = "\([^"]*\)"/\1/p' \
        "$act_root/framework/src/act/config.py"
    )"
    if [[ "$act_required_sail" != "$sail_expected_version" ]]; then
      echo "ACT framework/Sail mismatch: this checkout requires Sail $act_required_sail, but Jenkins is configured for $sail_expected_version." >&2
      echo "Update riscv-arch-test to a Sail-$sail_expected_version-compatible revision before starting the job." >&2
      exit 1
    fi
    configured_sail="$(awk -F': *' '/^ref_model_exe:/ {print $2; exit}' "$act_root/$act_config" | awk '{print $1}')"
    if [[ "$(readlink -f "$configured_sail")" != "$(readlink -f "$sail_bin")" ]]; then
      echo "ACT config uses $configured_sail, expected $sail_bin" >&2
      exit 1
    fi
    command -v spike
    free_kb="$(df -Pk "$repo_root" | awk 'NR == 2 { print $4 }')"
    if (( free_kb < 6 * 1024 * 1024 )); then
      echo "At least 6 GiB of free workspace space is required; found $((free_kb / 1024)) MiB." >&2
      exit 1
    fi
    python3 -m py_compile tools/act_agent/run_vf2_pack.py tools/act_agent/run_reference_elf.py
    python3 ci/jenkins/capture_provenance.py \
      --repo-root "$repo_root" \
      --act-root "$act_root" \
      --state-root "$state_root" \
      --phase preflight \
      --expected-act-revision "$resolved_act_revision"
    echo "Preflight passed for $run_id"
    ;;

  prepare)
    # This workdir is Jenkins-owned generated output. Recreate it so removed or
    # renamed tests can never be packed from stale artifacts.
    if [[ -d "$act_root/$act_workdir" ]]; then
      find "$act_root/$act_workdir" -depth -delete
    fi

    priv_generator_extensions="$(
      cd "$act_root"
      uv run python -c \
        'from testgen.priv import get_priv_test_extensions; print(",".join(sorted(get_priv_test_extensions())))'
    )"
    if [[ "${REGENERATE_TESTS:-true}" == "true" ]]; then
      if [[ -d "$generated_test_root" ]]; then
        find "$generated_test_root" -depth -delete
      fi
      (
        cd "$act_root"
        uv run testgen testplans -o "$generated_test_root" --jobs 1 \
          --extensions "$priv_generator_extensions" --exclude ''
      )
    elif [[ -d "$generated_test_root" ]]; then
      find "$generated_test_root" -depth -delete
    fi

    static_priv_suites="$(
      git -C "$act_root" ls-tree -d --name-only HEAD:tests/priv | paste -sd, -
    )"
    official_priv_suites="${priv_generator_extensions},${static_priv_suites}"
    printf '%s\n' "$official_priv_suites" | tr ',' '\n' > "$priv_source_roots"

    python3 ci/jenkins/stage_priv_tests.py \
      --source "$act_root/tests" \
      --repository-root "$act_root" \
      --generated-source "$generated_test_root" \
      --destination "$priv_test_dir" \
      --include-top-level "$static_priv_suites" \
      --include-generated-top-level "$priv_generator_extensions"

    python3 tools/act_agent/run_vf2_pack.py \
      --run-id "$run_id" \
      --act-config "$act_config" \
      --act-workdir "$act_workdir" \
      --extensions "$all_extensions" \
      --test-dir "$priv_test_dir" \
      --build-act-artifacts \
      --build-pack \
      --pack-elf-kind elf \
      --no-act-debug \
      --skip-build \
      --skip-sd-write \
      --skip-serial-run \
      --skip-triage \
      --skip-final-snapshot \
      --expected-cases 0

    python3 - "$artifact_root" "$pack_list" "$missing_report" "$reference_status" <<'PY'
from pathlib import Path
import sys

artifact_root = Path(sys.argv[1])
referenced = {
    path.name[:-8]
    for path in artifact_root.rglob("*.sig.elf")
}
packed = {
    path.name[:-4]
    for raw in Path(sys.argv[2]).read_text().splitlines()
    if raw.strip() and not raw.lstrip().startswith("#")
    for path in [Path(raw.strip())]
}
missing = sorted(referenced - packed)
Path(sys.argv[3]).write_text("".join(f"{name}\n" for name in missing))
rows = ["test_name\tsail_status\thardware_elf\n"]
rows.extend(f"{name}\tPASS\tyes\n" for name in sorted(packed))
rows.extend(f"{name}\tFAIL_OR_BLOCKED\tno\n" for name in missing)
Path(sys.argv[4]).write_text("".join(rows))
print(f"U74/Sail eligibility: reference_attempted={len(referenced)} runnable={len(packed)} reference_failed={len(missing)}")
if missing:
    print("Sail did not produce self-checking hardware ELFs for:")
    for name in missing:
        print(f"  {name}")
PY

    bash cert_harness/tools/build_profile.sh \
      --board vf2_jh7110 \
      --profile ACT_PRIV_M_OWN_ENV \
      --act-list "$pack_list" \
      --keep-make-outputs

    python3 ci/jenkins/capture_provenance.py \
      --repo-root "$repo_root" \
      --act-root "$act_root" \
      --state-root "$state_root" \
      --phase prepared \
      --expected-act-revision "$act_expected_revision" \
      --test-root "$priv_test_dir" \
      --pack-list "$pack_list" \
      --hardware-artifacts "$hardware_artifacts"
    write_state
    ;;

  spike)
    load_state
    mkdir -p "$REFERENCE_ROOT"
    total=0
    failed=0
    : > "$state_root/spike_status.tsv"
    while IFS= read -r elf; do
      [[ -n "$elf" && "${elf#\#}" == "$elf" ]] || continue
      name="$(basename "$elf" .elf)"
      out="$REFERENCE_ROOT/$name/spike"
      total=$((total + 1))
      if python3 tools/act_agent/run_reference_elf.py "$elf" \
          --model spike --execute --out-dir "$out" --spike-timeout 120; then
        printf '%s\tPASS\n' "$name" >> "$state_root/spike_status.tsv"
      else
        printf '%s\tFAIL\n' "$name" >> "$state_root/spike_status.tsv"
        failed=$((failed + 1))
      fi
    done < "$PACK_LIST"
    printf 'total=%s\nfailed=%s\n' "$total" "$failed" > "$state_root/spike_counts.txt"
    [[ "$failed" -eq 0 ]]
    ;;

  flash)
    load_state
    test -b "$sd_dev"
    test -f "$HARDWARE_ARTIFACTS/boot_image.bin"
    test -f "$HARDWARE_ARTIFACTS/act_pack.bin"
    # These two fixed helpers are the only passwordless hardware operations
    # granted to the Jenkins service account by the installer.
    sudo "$repo_root/vf2_act_flash.sh" \
      --image "$HARDWARE_ARTIFACTS/boot_image.bin" \
      --sd-dev "$sd_dev"
    sudo "$repo_root/write_pack_to_sd_tail.sh" \
      "$HARDWARE_ARTIFACTS/act_pack.bin" "$sd_dev"
    ;;

  run)
    load_state
    test -e "$serial_dev"
    python3 tools/act_agent/run_vf2_pack.py \
      --run-id "$RUN_ID" \
      --act-workdir "$ACT_WORKDIR" \
      --artifact-root "$ARTIFACT_ROOT" \
      --reference-root "$REFERENCE_ROOT" \
      --extensions "$all_extensions" \
      --test-dir "$PRIV_TEST_DIR" \
      --pack-list "$PACK_LIST" \
      --pack-file "$HARDWARE_ARTIFACTS/act_pack.bin" \
      --pack-elf-kind elf \
      --skip-build \
      --skip-sd-write \
      --serial-dev "$serial_dev" \
      --serial-timeout "$capture_timeout" \
      --expected-cases "$EXPECTED_CASES" \
      --yes
    if [[ -s "$MISSING_REPORT" ]]; then
      echo "Board run completed, but some U74-selected privileged tests were not runnable because their Sail reference failed:" >&2
      sed 's/^/  /' "$MISSING_REPORT" >&2
      exit 3
    fi
    ;;

  finalize)
    write_state
    {
      echo "run_id=$run_id"
      echo "completed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      echo "git_head=$(git rev-parse HEAD 2>/dev/null || true)"
      echo "git_dirty_count=$(git status --short | wc -l | tr -d ' ')"
      echo "act_git_head=$(git -C "$act_root" rev-parse HEAD 2>/dev/null || true)"
      echo "act_expected_revision=$act_expected_revision"
      echo "act_revision_match=$([[ "$(git -C "$act_root" rev-parse HEAD 2>/dev/null || true)" == "$(git -C "$act_root" rev-parse "${act_expected_revision}^{commit}" 2>/dev/null || true)" ]] && echo yes || echo no)"
      echo "act_git_dirty_count=$(git -C "$act_root" status --short | wc -l | tr -d ' ')"
      echo "act_git_tracked_dirty_count=$(git -C "$act_root" status --short --untracked-files=no | wc -l | tr -d ' ')"
      echo "sail_bin=$sail_bin"
      echo "sail_version=$("$sail_bin" --version 2>/dev/null | head -n 1 || true)"
    } > "$state_root/jenkins_manifest.txt"
    tracking_sheet_csv="$state_root/tracking_sheet_snapshot.csv"
    tracking_sheet_url="https://docs.google.com/spreadsheets/d/1BFZ4SnrCr6xdMNws5hIqELPz0wAP_lZDRgim_itaPZo/export?format=csv&gid=1043525459"
    if curl -fL --retry 2 --connect-timeout 15 --max-time 60 \
        -o "$tracking_sheet_csv.tmp" "$tracking_sheet_url"; then
      mv "$tracking_sheet_csv.tmp" "$tracking_sheet_csv"
      python3 ci/jenkins/compare_tracking_sheet.py \
        --sheet-csv "$tracking_sheet_csv" \
        --state-root "$state_root" \
        --run-root "$repo_root/logs/runs/$run_id" \
        --issue-catalog ci/jenkins/test_issue_links.json \
        --output "$state_root/tracking_sheet_comparison.csv"
    else
      rm -f "$tracking_sheet_csv.tmp"
      echo "WARNING: tracking-sheet refresh failed; continuing without a comparison artifact." >&2
    fi
    python3 ci/jenkins/build_results_site.py \
      --workspace "$repo_root" \
      --state-root "$state_root" \
      --run-root "$repo_root/logs/runs/$run_id" \
      --artifact-root "$artifact_root" \
      --build-url "${BUILD_URL:-}"
    zip_inputs=("logs/jenkins/weekly/$run_id")
    if [[ -d "$repo_root/logs/runs/$run_id" ]]; then
      zip_inputs+=("logs/runs/$run_id")
    fi
    (
      cd "$repo_root"
      zip -rq "logs/jenkins/weekly/${run_id}-complete.zip" "${zip_inputs[@]}"
    )
    mkdir -p "$state_root/site/downloads"
    cp -f "$repo_root/logs/jenkins/weekly/${run_id}-complete.zip" \
      "$state_root/site/downloads/${run_id}-complete.zip"
    ;;

  *)
    echo "Usage: $0 {preflight|prepare|spike|flash|run|finalize}" >&2
    exit 2
    ;;
esac
