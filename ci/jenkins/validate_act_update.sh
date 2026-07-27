#!/usr/bin/env bash
set -euo pipefail

repo_root="${REPO_ROOT:-/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified}"
act_root="$repo_root/external/riscv-arch-test"
stage="${1:-}"
build_number="${BUILD_NUMBER:-manual}"
run_id="act_update_${build_number}"
state_root="$repo_root/logs/jenkins/act-update/$run_id"
state_file="$state_root/state.env"
candidate_root="$state_root/act-candidate"
generated_root="$state_root/generated_tests"
staged_root="$state_root/all_priv_tests"
act_workdir="$state_root/act-work"
official_url="${ACT_OFFICIAL_URL:-https://github.com/riscv/riscv-arch-test.git}"
official_branch="${OFFICIAL_BRANCH:-act4}"
vf2_base_revision="${VF2_BASE_REVISION:-4f3b59a9e7e1e0d5b2e35158e6ad0fcec7809f3f}"
sail_bin="${SAIL_BIN:-/home/lpt-10xe/riscv-sail-0.13/bin/sail_riscv_sim}"
sail_expected_version="${SAIL_EXPECTED_VERSION:-0.13}"
official_ref="refs/remotes/official/$official_branch"

mkdir -p "$state_root"

write_state() {
  {
    printf 'RUN_ID=%q\n' "$run_id"
    printf 'CANDIDATE_ROOT=%q\n' "$candidate_root"
    printf 'GENERATED_ROOT=%q\n' "$generated_root"
    printf 'STAGED_ROOT=%q\n' "$staged_root"
    printf 'ACT_WORKDIR=%q\n' "$act_workdir"
    printf 'VF2_BASE_REVISION=%q\n' "$vf2_base_revision"
    printf 'OFFICIAL_BRANCH=%q\n' "$official_branch"
    printf 'OFFICIAL_REF=%q\n' "$official_ref"
    if git -C "$act_root" rev-parse --verify "${official_ref}^{commit}" >/dev/null 2>&1; then
      printf 'OFFICIAL_REVISION=%q\n' "$(git -C "$act_root" rev-parse "$official_ref")"
    fi
  } > "$state_file"
}

load_state() {
  [[ -f "$state_file" ]] || {
    echo "Missing update-validation state: $state_file" >&2
    exit 1
  }
  # shellcheck disable=SC1090
  source "$state_file"
}

remove_candidate() {
  if [[ -e "$candidate_root/.git" ]]; then
    git -C "$act_root" worktree remove --force "$candidate_root" >/dev/null 2>&1 || true
  fi
}

case "$stage" in
  merge)
    test -d "$act_root/.git" || test -f "$act_root/.git"
    test -x "$sail_bin"
    if ! git -C "$act_root" rev-parse --verify "${vf2_base_revision}^{commit}" >/dev/null 2>&1; then
      echo "VF2_BASE_REVISION is not available locally: $vf2_base_revision" >&2
      exit 1
    fi
    if ! git -C "$act_root" diff --quiet --ignore-submodules -- ||
       ! git -C "$act_root" diff --cached --quiet --ignore-submodules --; then
      echo "Tracked changes exist in the primary ACT checkout; update validation will not risk mixing them into the candidate." >&2
      git -C "$act_root" status --short --untracked-files=no >&2
      exit 1
    fi

    git -C "$act_root" fetch --force "$official_url" \
      "+refs/heads/$official_branch:$official_ref"
    official_revision="$(git -C "$act_root" rev-parse "$official_ref")"
    vf2_revision="$(git -C "$act_root" rev-parse "${vf2_base_revision}^{commit}")"

    remove_candidate
    git -C "$act_root" worktree add --detach "$candidate_root" "$vf2_revision"
    set +e
    git -C "$candidate_root" \
      -c user.name='VF2 ACT Update Validator' \
      -c user.email='vf2-act-validator@localhost' \
      merge --no-commit --no-ff "$official_revision" \
      >"$state_root/merge.log" 2>&1
    merge_rc=$?
    set -e

    git -C "$candidate_root" status --short > "$state_root/candidate_status.txt"
    {
      echo "vf2_base_revision=$vf2_revision"
      echo "official_url=$official_url"
      echo "official_branch=$official_branch"
      echo "official_revision=$official_revision"
      echo "merge_base=$(git -C "$act_root" merge-base "$vf2_revision" "$official_revision")"
      echo "premerge_left_right=$(git -C "$act_root" rev-list --left-right --count "$vf2_revision...$official_revision")"
      echo "merge_rc=$merge_rc"
      echo "sail_expected_version=$sail_expected_version"
      echo "sail_version=$("$sail_bin" --version | head -n 1 | tr -d '\r')"
    } > "$state_root/update_manifest.txt"

    if [[ "$merge_rc" -ne 0 ]]; then
      git -C "$candidate_root" diff --name-only --diff-filter=U \
        > "$state_root/merge_conflicts.txt"
      git -C "$candidate_root" diff --binary \
        > "$state_root/merge_conflict_worktree.patch"
      echo "Official ACT cannot yet be merged cleanly into VF2 revision $vf2_revision." >&2
      echo "Conflict evidence: $state_root/merge_conflicts.txt" >&2
      exit "$merge_rc"
    fi

    git -C "$candidate_root" diff --cached --binary HEAD \
      > "$state_root/official_merge_candidate.patch"
    git -C "$candidate_root" diff --cached --binary "$official_revision" \
      > "$state_root/vf2_delta_against_new_official.patch"

    required_sail="$(
      sed -n 's/^REQUIRED_SAIL_VERSION = "\([^"]*\)"/\1/p' \
        "$candidate_root/framework/src/act/config.py"
    )"
    if [[ "$required_sail" != "$sail_expected_version" ]]; then
      echo "Latest official ACT requires Sail $required_sail, but validation is configured for $sail_expected_version." >&2
      exit 1
    fi
    write_state
    ;;

  generate)
    load_state
    test -d "$CANDIDATE_ROOT"
    rm -rf "$GENERATED_ROOT" "$STAGED_ROOT"

    generator_extensions="$(
      cd "$CANDIDATE_ROOT"
      uv run python -c \
        'from testgen.priv import get_priv_test_extensions; print(",".join(sorted(get_priv_test_extensions())))'
    )"
    (
      cd "$CANDIDATE_ROOT"
      uv run testgen testplans -o "$GENERATED_ROOT" --jobs 1 \
        --extensions "$generator_extensions" --exclude ''
    )

    static_suites="$(
      git -C "$CANDIDATE_ROOT" ls-files 'tests/priv/*' |
        awk -F/ 'NF >= 4 {print $3}' |
        sort -u |
        paste -sd, -
    )"
    python3 "$repo_root/ci/jenkins/stage_priv_tests.py" \
      --source "$CANDIDATE_ROOT/tests" \
      --repository-root "$CANDIDATE_ROOT" \
      --generated-source "$GENERATED_ROOT" \
      --destination "$STAGED_ROOT" \
      --include-top-level "$static_suites" \
      --include-generated-top-level "$generator_extensions"

    {
      echo "generator_extensions=$generator_extensions"
      echo "static_suites=$static_suites"
      echo "generated_test_count=$(find "$GENERATED_ROOT/priv" -type f -name '*.S' | wc -l | tr -d ' ')"
      echo "staged_test_count=$(find -L "$STAGED_ROOT/priv" -type f -name '*.S' | wc -l | tr -d ' ')"
    } > "$state_root/generation_counts.txt"
    ;;

  sail)
    load_state
    test -d "$CANDIDATE_ROOT"
    test -d "$STAGED_ROOT/priv"
    rm -rf "$ACT_WORKDIR"
    mkdir -p "$ACT_WORKDIR"

    set +e
    (
      cd "$CANDIDATE_ROOT"
      uv run act config/cores/sifive_u74/test_config.local.yaml \
        --workdir "$ACT_WORKDIR" \
        --test-dir "$STAGED_ROOT" \
        --extensions all \
        --jobs 0 \
        --keep-going
    ) 2>&1 | tee "$state_root/sail_validation.log"
    sail_rc="${PIPESTATUS[0]}"
    set -e

    {
      echo "act_rc=$sail_rc"
      echo "sig_elf_count=$(find "$ACT_WORKDIR" -type f -name '*.sig.elf' | wc -l | tr -d ' ')"
      echo "hardware_elf_count=$(find "$ACT_WORKDIR" -type f -name '*.elf' ! -name '*.sig.elf' | wc -l | tr -d ' ')"
      echo "results_count=$(find "$ACT_WORKDIR" -type f -name '*.results' | wc -l | tr -d ' ')"
    } > "$state_root/sail_counts.txt"
    exit "$sail_rc"
    ;;

  finalize)
    write_state
    {
      echo "completed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
      echo "validator_runner_head=$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || true)"
      echo "source_act_head=$(git -C "$act_root" rev-parse HEAD 2>/dev/null || true)"
    } > "$state_root/finalize.txt"
    remove_candidate
    ;;

  *)
    echo "Usage: $0 {merge|generate|sail|finalize}" >&2
    exit 2
    ;;
esac
